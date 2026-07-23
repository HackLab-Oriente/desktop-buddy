// WS2812 RGB ring mood backend — the v1 indicator. The whole ring glows the
// emotion's mood color (shared with the eyes via face_model.h) and animates
// per led.mood: breathe (calm/excited), a rotating comet (thinking), or off.
// Brightness is hard-capped here — the power governor's citizen for the ring.
// Compiled only when selected in menuconfig.
#include "sdkconfig.h"
#if CONFIG_BUDDY_LED_WS2812

#include "bus.h"
#include "expressions.h"
#include "face_model.h"

#include <cmath>
#include <string>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led_strip.h"

static const char* TAG = "ring";

namespace buddy {
namespace {

// 12 LEDs of white would pull ~700 mA; cap brightness so the ring is a mood
// glow, not a flashlight. Raise only if the ring has its own 5 V supply.
constexpr float kMaxBright = 0.35f;
constexpr int kFrameMs = 40;

led_strip_handle_t s_strip = nullptr;
int s_count = 12;
volatile uint8_t s_r = 0, s_g = 205, s_b = 255;  // neutral cyan
volatile int s_mood = 0;                          // index into kMoods

struct Mood { const char* name; int period_ms; float floor; bool chase; };
constexpr Mood kMoods[] = {
    {"calm",     3200, 0.06f, false},
    {"excited",  700,  0.18f, false},
    {"thinking", 0,    0.0f,  true},
    {"off",      0,    0.0f,  false},
};

void set_all(float bright) {
  const uint8_t r = static_cast<uint8_t>(s_r * bright * kMaxBright);
  const uint8_t g = static_cast<uint8_t>(s_g * bright * kMaxBright);
  const uint8_t b = static_cast<uint8_t>(s_b * bright * kMaxBright);
  for (int i = 0; i < s_count; i++) led_strip_set_pixel(s_strip, i, r, g, b);
  led_strip_refresh(s_strip);
}

void ring_task(void*) {
  float phase = 0;
  int head = 0;
  for (;;) {
    const Mood& m = kMoods[s_mood];
    if (std::string(m.name) == "off") {
      led_strip_clear(s_strip);
    } else if (m.chase) {  // thinking: a comet chasing around the ring
      for (int i = 0; i < s_count; i++) {
        int d = abs(i - head);
        if (d > s_count - d) d = s_count - d;      // wrap distance
        const float tail = 1.0f - d * 0.55f;
        const float br = (tail > 0 ? tail : 0) * kMaxBright;
        led_strip_set_pixel(s_strip, i, static_cast<uint8_t>(s_r * br),
                            static_cast<uint8_t>(s_g * br),
                            static_cast<uint8_t>(s_b * br));
      }
      led_strip_refresh(s_strip);
      head = (head + 1) % s_count;
    } else {  // calm / excited: whole ring breathes
      phase += static_cast<float>(kFrameMs) / m.period_ms;
      const float s = (sinf(phase * 2 * 3.14159265f) + 1) / 2;  // 0..1
      set_all(m.floor + s * (1.0f - m.floor));
    }
    vTaskDelay(pdMS_TO_TICKS(kFrameMs));
  }
}

}  // namespace

void led_start() {
  s_count = CONFIG_BUDDY_WS2812_COUNT;

  led_strip_config_t cfg = {};
  cfg.strip_gpio_num = CONFIG_BUDDY_WS2812_PIN;
  cfg.max_leds = s_count;
  cfg.color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB;
  cfg.led_model = LED_MODEL_WS2812;
  cfg.flags.invert_out = false;

  led_strip_rmt_config_t rmt = {};
  rmt.clk_src = RMT_CLK_SRC_DEFAULT;
  rmt.resolution_hz = 10 * 1000 * 1000;
  rmt.flags.with_dma = false;
  ESP_ERROR_CHECK(led_strip_new_rmt_device(&cfg, &rmt, &s_strip));
  led_strip_clear(s_strip);

  // Ring color follows the emotion's mood color (same table as the eyes).
  bus().subscribe("face.emotion", [](const Event& ev) {
    int i = emotion_index(ev.payload.c_str());
    if (i >= 0) { s_r = kEmotions[i].r; s_g = kEmotions[i].g; s_b = kEmotions[i].b; }
  });
  // Animation follows the mood.
  bus().subscribe("led.mood", [](const Event& ev) {
    for (size_t i = 0; i < sizeof kMoods / sizeof kMoods[0]; i++)
      if (ev.payload == kMoods[i].name) { s_mood = static_cast<int>(i); return; }
  });

  ESP_LOGI(TAG, "WS2812 ring: %d LEDs on GPIO %d", s_count, CONFIG_BUDDY_WS2812_PIN);
  xTaskCreatePinnedToCore(ring_task, "ring", 3072, nullptr, 3, nullptr, 1);
}

}  // namespace buddy

#endif  // CONFIG_BUDDY_LED_WS2812
