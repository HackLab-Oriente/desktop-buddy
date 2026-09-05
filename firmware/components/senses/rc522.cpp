// MFRC522: detect an ISO14443A tag, read its UID and any NDEF text.
//
// The firmware stops at "here is a string" -- cartridge grammar lives in a
// Berry reflex, so a new verb costs an edit and not a reflash (#24).
#include "bus.h"
#include "ndef.h"
#include "senses.h"

#include <cstdint>
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
  CommandReg = 0x01, ComIrqReg = 0x04, DivIrqReg = 0x05, ErrorReg = 0x06,
  FIFODataReg = 0x09, FIFOLevelReg = 0x0A, BitFramingReg = 0x0D, ModeReg = 0x11,
  TxControlReg = 0x14, TxASKReg = 0x15, CRCResultRegH = 0x21,
  CRCResultRegL = 0x22, TModeReg = 0x2A, TPrescalerReg = 0x2B,
  TReloadRegH = 0x2C, TReloadRegL = 0x2D, VersionReg = 0x37,
};
enum Cmd : uint8_t { Idle = 0x00, CalcCRC = 0x03, Transceive = 0x0C, SoftReset = 0x0F };

constexpr int kMaxText = 128;
// Enough pages that a full kMaxText payload fits with room for the TLV and
// record headers; the read window used to be 64 bytes, which capped real text
// at 56 while the registry promised 128.
constexpr int kReadBytes = 160;

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

// SELECT and READ carry a CRC_A the reader computes for us.
void calc_crc(const uint8_t* data, uint8_t len, uint8_t* out) {
  wr(CommandReg, Idle);
  wr(DivIrqReg, 0x04);                       // clear CRCIRq
  wr(FIFOLevelReg, 0x80);
  for (int i = 0; i < len; i++) wr(FIFODataReg, data[i]);
  wr(CommandReg, CalcCRC);
  for (int i = 0; i < 100 && !(rd(DivIrqReg) & 0x04); i++) vTaskDelay(1);
  wr(CommandReg, Idle);
  out[0] = rd(CRCResultRegL);
  out[1] = rd(CRCResultRegH);
}

// Anticollision + SELECT for one cascade level. Fills uid_out with this
// level's 4 bytes. Returns the SAK, or -1.
int select_level(uint8_t cascade_cmd, uint8_t* uid_out) {
  uint8_t ac[2] = {cascade_cmd, 0x20}, resp[5];
  if (transceive(ac, 2, 0, resp, 5) != 5) return -1;
  if ((resp[0] ^ resp[1] ^ resp[2] ^ resp[3]) != resp[4]) {
    ESP_LOGD(TAG, "BCC mismatch — collision or noise");   // two tags look like none
    return -1;
  }
  memcpy(uid_out, resp, 4);

  uint8_t sel[9] = {cascade_cmd, 0x70, resp[0], resp[1], resp[2], resp[3], resp[4]};
  calc_crc(sel, 7, sel + 7);
  uint8_t sak[3];
  if (transceive(sel, 9, 0, sak, 3) < 1) return -1;
  return sak[0];
}

// Full anticollision. Writes the real UID (4 or 7 bytes) and returns its
// length, or -1. The old driver did cascade level 1 only and reported the
// cascade tag as if it were UID bytes, so every 7-byte tag — which is every
// NTAG — came out as 0x88 plus three bytes of somebody else's identity.
int select_tag(uint8_t* uid) {
  uint8_t l1[4];
  const int sak1 = select_level(0x93, l1);
  if (sak1 < 0) return -1;
  if (!(sak1 & 0x04)) {                      // no cascade: 4-byte UID
    memcpy(uid, l1, 4);
    return 4;
  }
  // Cascade: l1[0] is the cascade tag 0x88, the real UID starts at l1[1].
  uint8_t l2[4];
  if (select_level(0x95, l2) < 0) return -1;
  memcpy(uid, l1 + 1, 3);
  memcpy(uid + 3, l2, 4);
  return 7;
}

// Type 2 READ: returns 4 pages (16 bytes) starting at `page`.
bool read_pages(uint8_t page, uint8_t* out16) {
  uint8_t cmd[4] = {0x30, page};
  calc_crc(cmd, 2, cmd + 2);
  uint8_t buf[18];
  const int n = transceive(cmd, 4, 0, buf, sizeof buf);
  if (n < 16) return false;
  memcpy(out16, buf, 16);
  return true;
}

void halt() {
  uint8_t h[4] = {0x50, 0x00};
  calc_crc(h, 2, h + 2);
  uint8_t ignored[2];
  transceive(h, 4, 0, ignored, sizeof ignored);  // a HALT is answered by silence
}

// Read user memory (from page 4) and decode it. Bounded on purpose: enough for
// a label or a URL, nowhere near NTAG215's 504 bytes.
int read_ndef(char* out, int out_max) {
  uint8_t buf[kReadBytes];
  int have = 0;
  for (uint8_t page = 4; have + 16 <= static_cast<int>(sizeof buf); page += 4) {
    if (!read_pages(page, buf + have)) break;
    // Only the new pages: 0xFE is a terminator in TLV position and an ordinary
    // data byte anywhere else.
    const bool done = memchr(buf + have, 0xFE, 16) != nullptr;
    have += 16;
    if (done) break;
  }
  if (have == 0) return 0;
  return ndef::first_record(buf, have, out, out_max);
}

// The log's other end is a terminal that honours escape sequences.
void strip_controls(char* s) {
  for (; *s; s++)
    if (static_cast<unsigned char>(*s) < 0x20 || static_cast<unsigned char>(*s) == 0x7F)
      *s = ' ';
}

void rc522_task(void*) {
  char last_uid[16] = {0};
  int misses = 0;

  for (;;) {
    // WUPA rather than REQA: a tag we already selected is in HALT, and only a
    // wake-up call brings it back. With REQA a tag left on the reader would
    // read as absent, which would make nfc.gone fire while it is still there.
    uint8_t wupa = 0x52, atqa[2];
    uint8_t uid[7];
    int uid_len = -1;
    if (transceive(&wupa, 1, 7, atqa, 2) == 2) uid_len = select_tag(uid);

    if (uid_len > 0) {
      char hex[16] = {0};
      for (int i = 0; i < uid_len; i++) snprintf(hex + i * 2, 3, "%02x", uid[i]);

      if (strcmp(hex, last_uid) != 0) {      // a new arrival, not a hold
        // A swap inside one poll used to emit tag A then tag B with no gone.
        if (last_uid[0]) bus().publish("nfc.gone", "");
        strcpy(last_uid, hex);
        bus().publish("nfc.tag", hex);
        ESP_LOGI(TAG, "tag %s", hex);

        char text[kMaxText];
        if (read_ndef(text, sizeof text) > 0) {
          bus().publish("nfc.text", text);
          strip_controls(text);
          ESP_LOGI(TAG, "text \"%s\"", text);
        }
      }
      misses = 0;
      halt();                                // release so the next WUPA works
    } else if (last_uid[0]) {
      // Hysteresis: a single missed poll is RF noise, not a departure. Three
      // in a row (~600 ms) is someone lifting the card.
      if (++misses >= 3) {
        misses = 0;
        last_uid[0] = '\0';
        bus().publish("nfc.gone", "");
        ESP_LOGI(TAG, "gone");
      }
    }
    vTaskDelay(pdMS_TO_TICKS(200));
  }
}

}  // namespace

// Never ESP_ERROR_CHECK: a loose wire aborted the main task and the buddy came
// up black until someone found the cable.
#define TRY(expr, what)                                                   \
  do {                                                                    \
    const esp_err_t _e = (expr);                                          \
    if (_e != ESP_OK) {                                                   \
      ESP_LOGE(TAG, "%s: %s — no NFC", what, esp_err_to_name(_e));        \
      return false;                                                       \
    }                                                                     \
  } while (0)

bool nfc_start(const Rc522Pins& p) {
  gpio_set_direction(static_cast<gpio_num_t>(p.rst), GPIO_MODE_OUTPUT);
  gpio_set_level(static_cast<gpio_num_t>(p.rst), 1);

  spi_bus_config_t bus_cfg = {};
  bus_cfg.sclk_io_num = p.sck;
  bus_cfg.miso_io_num = p.miso;
  bus_cfg.mosi_io_num = p.mosi;
  bus_cfg.quadwp_io_num = -1;
  bus_cfg.quadhd_io_num = -1;
  TRY(spi_bus_initialize(SPI3_HOST, &bus_cfg, SPI_DMA_CH_AUTO), "SPI3 bus");

  spi_device_interface_config_t dev_cfg = {};
  dev_cfg.clock_speed_hz = 4 * 1000 * 1000;
  dev_cfg.mode = 0;
  dev_cfg.spics_io_num = p.cs;
  dev_cfg.queue_size = 4;
  TRY(spi_bus_add_device(SPI3_HOST, &dev_cfg, &s_dev), "RC522 device");

  wr(CommandReg, SoftReset);
  vTaskDelay(pdMS_TO_TICKS(50));
  wr(TModeReg, 0x8D);        // timer: auto, prescaler high bits
  wr(TPrescalerReg, 0x3E);
  wr(TReloadRegH, 0x00);
  wr(TReloadRegL, 30);
  wr(TxASKReg, 0x40);        // 100% ASK
  wr(ModeReg, 0x3D);         // CRC preset 0x6363
  wr(TxControlReg, rd(TxControlReg) | 0x03);  // antenna on

  const uint8_t ver = rd(VersionReg);
  if (ver != 0x91 && ver != 0x92) {
    ESP_LOGE(TAG, "no MFRC522 (VersionReg 0x%02x) — check wiring", ver);
    return false;
  }
  ESP_LOGI(TAG, "MFRC522 version 0x%02x", ver);
  xTaskCreatePinnedToCore(rc522_task, "rc522", 4096, nullptr, 4, nullptr, 1);
  return true;
}
#undef TRY

}  // namespace buddy
