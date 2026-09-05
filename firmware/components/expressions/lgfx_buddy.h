// LovyanGFX device config for the buddy's GC9A01, driven by the menuconfig
// pins so there is still exactly one place to change wiring.
//
// Note what is NOT here: no mirror call. The esp_lcd path needed
// esp_lcd_panel_mirror(panel, true, false) because the panel came up
// horizontally flipped — text read backwards and the brow slants inverted.
// LovyanGFX gets GC9A01 orientation and colour order right unaided; that was
// verified on hardware with a test card before this migration.
#pragma once
#define LGFX_USE_V1
#include <LovyanGFX.hpp>

#include "driver/spi_common.h"
#include "esp_log.h"
#include "sdkconfig.h"

class LGFX_Buddy : public lgfx::LGFX_Device {
  lgfx::Panel_GC9A01 _panel;
  lgfx::Bus_SPI _bus;
#if CONFIG_BUDDY_GC9A01_BL >= 0
  lgfx::Light_PWM _light;
#endif

 public:
  LGFX_Buddy() {
    {
      auto cfg = _bus.config();
      cfg.spi_host = SPI2_HOST;
      cfg.spi_mode = 0;
      cfg.freq_write = 40000000;
      cfg.freq_read = 16000000;
      cfg.spi_3wire = true;
      cfg.use_lock = true;
      cfg.dma_channel = SPI_DMA_CH_AUTO;
      cfg.pin_sclk = CONFIG_BUDDY_GC9A01_SCLK;
      cfg.pin_mosi = CONFIG_BUDDY_GC9A01_MOSI;
      cfg.pin_miso = -1;
      cfg.pin_dc = CONFIG_BUDDY_GC9A01_DC;
      _bus.config(cfg);
      _panel.setBus(&_bus);
    }
    {
      auto cfg = _panel.config();
      cfg.pin_cs = CONFIG_BUDDY_GC9A01_CS;
      cfg.pin_rst = CONFIG_BUDDY_GC9A01_RST;
      cfg.pin_busy = -1;
      cfg.panel_width = 240;
      cfg.panel_height = 240;
      cfg.offset_x = 0;
      cfg.offset_y = 0;
      cfg.offset_rotation = 0;
      cfg.readable = false;
      cfg.invert = true;        // GC9A01 clones want this
      cfg.rgb_order = false;    // false = BGR
      cfg.dlen_16bit = false;
      cfg.bus_shared = false;
      _panel.config(cfg);
    }
#if CONFIG_BUDDY_GC9A01_BL >= 0
    {
      auto cfg = _light.config();
      cfg.pin_bl = CONFIG_BUDDY_GC9A01_BL;
      cfg.invert = false;
      cfg.freq = 44100;
      cfg.pwm_channel = 7;
      _light.config(cfg);
      _panel.setLight(&_light);
    }
#endif
    setPanel(&_panel);
  }

  // Claim the SPI bus ourselves, before init().
  //
  // LovyanGFX fills spi_bus_config_t with 0xFF and then writes back only the
  // fields it knows about. Every int it leaves alone becomes -1, which is the
  // correct "unused" sentinel for a pin — so this was harmless until ESP-IDF
  // 6.1 added dma_burst_size, which is not a pin. GDMA gets 0xFFFFFFFF,
  // rejects it as not a power of two, and spi_bus_initialize() then panics in
  // its own cleanup path, freeing a DMA context it never allocated. The board
  // reboots before a single pixel.
  //
  // Initialising the bus here first means LovyanGFX's own call bails at its
  // very first check with ESP_ERR_INVALID_STATE, before it can reach the
  // 0xFF struct, and it attaches its device to this bus instead. Bus_SPI::init
  // still finds its GDMA channel afterwards by scanning the peripheral
  // registers (search_dma_out_ch), which is the non-obvious reason the
  // hand-off works at all.
  //
  // Two log lines per boot are expected, and neither is a fault:
  //   E spi: spi_bus_initialize(897): SPI bus already initialized.
  //   W LGFX: Failed to spi_bus_initialize.
  //
  // Delete this when the following prints 0:
  //   grep -c 'memset(&buscfg' components/LovyanGFX/src/lgfx/v1/platforms/esp32/common.cpp
  // Still 1 on upstream develop @57ca5a7, checked 2026-09-05. Not needed on
  // ESP-IDF <= 6.0 (no such field), nor on chips without a configurable GDMA
  // burst size, where spi_common.c ignores the field outright.
  //
  // The pins come off _bus.config() and not from CONFIG_* so that the header
  // above stays the one place where the wiring lives. senses/rc522.cpp does
  // the same thing the easy way — its spi_bus_config_t is `= {}` already.
  bool claim_bus() {
    const auto& b = _bus.config();
    spi_bus_config_t cfg = {};  // the whole point: actually zero-initialised
    cfg.mosi_io_num = b.pin_mosi;
    cfg.miso_io_num = b.pin_miso;
    cfg.sclk_io_num = b.pin_sclk;
    cfg.quadwp_io_num = -1;  // unused pins must say -1: 0 is a real GPIO
    cfg.quadhd_io_num = -1;
    cfg.data4_io_num = -1;
    cfg.data5_io_num = -1;
    cfg.data6_io_num = -1;
    cfg.data7_io_num = -1;
    cfg.max_transfer_sz = 1;  // what LovyanGFX asks for itself: it programs
                              // GDMA directly and never uses the IDF
                              // transaction path this would size a pool for
    cfg.flags = SPICOMMON_BUSFLAG_MASTER;
    const esp_err_t err = spi_bus_initialize(
        static_cast<spi_host_device_t>(b.spi_host), &cfg,
        static_cast<spi_dma_chan_t>(b.dma_channel));
    // Never ESP_ERROR_CHECK. This is the first hardware call of the boot, so
    // an abort here is the black screen and silent reboot loop this function
    // exists to prevent — and it would take the web UI with it, which is the
    // only way to recover without a serial cable. If it failed, LovyanGFX is
    // about to try for itself and log its own complaint.
    if (err != ESP_OK)
      ESP_LOGW("LGFX_Buddy", "could not claim SPI%d: %s", b.spi_host + 1,
               esp_err_to_name(err));
    return err == ESP_OK;
  }
};
