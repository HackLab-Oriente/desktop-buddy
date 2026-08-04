// LovyanGFX device config for the buddy's exact wiring, so anything measured
// here transfers to the real board without a pin translation step.
// Pins match firmware/main/Kconfig.projbuild (GC9A01 round display, SPI2).
#pragma once
#define LGFX_USE_V1
#include <LovyanGFX.hpp>

class LGFX_Buddy : public lgfx::LGFX_Device {
  lgfx::Panel_GC9A01 _panel;
  lgfx::Bus_SPI _bus;
  lgfx::Light_PWM _light;

 public:
  LGFX_Buddy() {
    {
      auto cfg = _bus.config();
      cfg.spi_host = SPI2_HOST;
      cfg.spi_mode = 0;
      cfg.freq_write = 40000000;  // our esp_lcd path runs 40 MHz too
      cfg.freq_read = 16000000;
      cfg.spi_3wire = true;
      cfg.use_lock = true;
      cfg.dma_channel = SPI_DMA_CH_AUTO;
      cfg.pin_sclk = 12;
      cfg.pin_mosi = 11;
      cfg.pin_miso = -1;
      cfg.pin_dc = 9;
      _bus.config(cfg);
      _panel.setBus(&_bus);
    }
    {
      auto cfg = _panel.config();
      cfg.pin_cs = 10;
      cfg.pin_rst = 8;
      cfg.pin_busy = -1;
      cfg.panel_width = 240;
      cfg.panel_height = 240;
      cfg.offset_x = 0;
      cfg.offset_y = 0;
      cfg.offset_rotation = 0;
      cfg.readable = false;
      // These two are the whole point of test 1. Our esp_lcd path needed
      // invert=true + BGR + a horizontal mirror. If LovyanGFX gets GC9A01
      // right out of the box, that regression risk is retired.
      cfg.invert = true;
      cfg.rgb_order = false;  // false = BGR
      cfg.dlen_16bit = false;
      cfg.bus_shared = false;
      _panel.config(cfg);
    }
    {
      auto cfg = _light.config();
      cfg.pin_bl = 7;
      cfg.invert = false;
      cfg.freq = 44100;
      cfg.pwm_channel = 7;
      _light.config(cfg);
      _panel.setLight(&_light);
    }
    setPanel(&_panel);
  }
};
