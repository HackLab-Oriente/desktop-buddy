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
};
