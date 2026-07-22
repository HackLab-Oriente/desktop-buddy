// Minimal MFRC522 driver: detect an ISO14443A tag, read its UID, emit it.
// Deliberately tiny — request + anticollision (cascade level 1) only.
// Good enough for 4-byte-UID MIFARE Classic fobs; 7-byte NTAG UIDs come
// out truncated to the cascade tag (fine for registry PoC, fix in v1).
#include "bus.h"
#include "senses.h"

#include <cstdio>
#include <cstring>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "rc522";

namespace buddy {
namespace {

// MFRC522 registers (datasheet §9)
enum Reg : uint8_t {
  CommandReg = 0x01, ComIrqReg = 0x04, ErrorReg = 0x06, FIFODataReg = 0x09,
  FIFOLevelReg = 0x0A, BitFramingReg = 0x0D, ModeReg = 0x11, TxControlReg = 0x14,
  TxASKReg = 0x15, TModeReg = 0x2A, TPrescalerReg = 0x2B, TReloadRegH = 0x2C,
  TReloadRegL = 0x2D, VersionReg = 0x37,
};
enum Cmd : uint8_t { Idle = 0x00, Transceive = 0x0C, SoftReset = 0x0F };

spi_device_handle_t s_dev;

void wr(uint8_t reg, uint8_t val) {
  uint8_t tx[2] = {static_cast<uint8_t>((reg << 1) & 0x7E), val};
  spi_transaction_t t = {};
  t.length = 16;
  t.tx_buffer = tx;
  spi_device_transmit(s_dev, &t);
}

uint8_t rd(uint8_t reg) {
  uint8_t tx[2] = {static_cast<uint8_t>(((reg << 1) & 0x7E) | 0x80), 0};
  uint8_t rx[2] = {};
  spi_transaction_t t = {};
  t.length = 16;
  t.tx_buffer = tx;
  t.rx_buffer = rx;
  spi_device_transmit(s_dev, &t);
  return rx[1];
}

// Transceive `send` and collect the response. Returns response length in
// bytes, or -1 on timeout/error.
int transceive(const uint8_t* send, uint8_t send_len, uint8_t tx_last_bits,
               uint8_t* recv, uint8_t recv_max) {
  wr(CommandReg, Idle);
  wr(ComIrqReg, 0x7F);                       // clear irqs
  wr(FIFOLevelReg, 0x80);                    // flush FIFO
  for (int i = 0; i < send_len; i++) wr(FIFODataReg, send[i]);
  wr(BitFramingReg, tx_last_bits & 0x07);
  wr(CommandReg, Transceive);
  wr(BitFramingReg, (tx_last_bits & 0x07) | 0x80);  // StartSend

  for (int i = 0; i < 40; i++) {             // ~40 ms budget
    uint8_t irq = rd(ComIrqReg);
    if (irq & 0x30) {                        // RxIRq or IdleIRq
      if (rd(ErrorReg) & 0x13) return -1;    // BufferOvfl | ParityErr | ProtocolErr
      uint8_t n = rd(FIFOLevelReg);
      if (n > recv_max) n = recv_max;
      for (int j = 0; j < n; j++) recv[j] = rd(FIFODataReg);
      return n;
    }
    if (irq & 0x01) return -1;               // TimerIRq
    vTaskDelay(1);
  }
  return -1;
}

void rc522_task(void*) {
  char last_uid[16] = {0};
  int64_t last_seen = 0;
  for (;;) {
    // REQA (0x26, 7 bits) — is any tag in the field?
    uint8_t reqa = 0x26, atqa[2];
    if (transceive(&reqa, 1, 7, atqa, 2) == 2) {
      // Anticollision CL1: 0x93 0x20 → 4 UID bytes + BCC
      uint8_t ac[2] = {0x93, 0x20}, resp[5];
      if (transceive(ac, 2, 0, resp, 5) == 5 &&
          (resp[0] ^ resp[1] ^ resp[2] ^ resp[3]) == resp[4]) {
        char uid[16];
        snprintf(uid, sizeof uid, "%02x%02x%02x%02x", resp[0], resp[1], resp[2], resp[3]);
        const int64_t now = esp_log_timestamp();
        // Debounce: same tag held on the reader fires once, not at 5 Hz.
        if (strcmp(uid, last_uid) != 0 || now - last_seen > 2000) {
          strcpy(last_uid, uid);
          bus().publish("nfc.tag", uid);
          ESP_LOGI(TAG, "tag %s", uid);
        }
        last_seen = now;
      }
    }
    vTaskDelay(pdMS_TO_TICKS(200));
  }
}

}  // namespace

void rc522_start(const Rc522Pins& p) {
  gpio_set_direction(static_cast<gpio_num_t>(p.rst), GPIO_MODE_OUTPUT);
  gpio_set_level(static_cast<gpio_num_t>(p.rst), 1);

  spi_bus_config_t bus_cfg = {};
  bus_cfg.sclk_io_num = p.sck;
  bus_cfg.miso_io_num = p.miso;
  bus_cfg.mosi_io_num = p.mosi;
  bus_cfg.quadwp_io_num = -1;
  bus_cfg.quadhd_io_num = -1;
  ESP_ERROR_CHECK(spi_bus_initialize(SPI3_HOST, &bus_cfg, SPI_DMA_CH_AUTO));

  spi_device_interface_config_t dev_cfg = {};
  dev_cfg.clock_speed_hz = 4 * 1000 * 1000;
  dev_cfg.mode = 0;
  dev_cfg.spics_io_num = p.cs;
  dev_cfg.queue_size = 4;
  ESP_ERROR_CHECK(spi_bus_add_device(SPI3_HOST, &dev_cfg, &s_dev));

  wr(CommandReg, SoftReset);
  vTaskDelay(pdMS_TO_TICKS(50));
  wr(TModeReg, 0x8D);        // timer: auto, prescaler high bits
  wr(TPrescalerReg, 0x3E);
  wr(TReloadRegH, 0x00);
  wr(TReloadRegL, 30);
  wr(TxASKReg, 0x40);        // 100% ASK
  wr(ModeReg, 0x3D);         // CRC preset 0x6363
  wr(TxControlReg, rd(TxControlReg) | 0x03);  // antenna on

  ESP_LOGI(TAG, "MFRC522 version 0x%02x", rd(VersionReg));  // expect 0x91/0x92
  xTaskCreatePinnedToCore(rc522_task, "rc522", 3072, nullptr, 4, nullptr, 1);
}

}  // namespace buddy
