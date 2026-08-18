// WS2812 RGB ring mood backend. The ring animates per led.mood; the mood's
// name is resolved against the pack's table (mood_model.h), not a compiled
// enum, so a pack invents as many moods as it likes out of five fixed
// primitives.
//
// Colour: a mood with no `colors` follows the current expression, which is
// what keeps the eyes and the halo agreeing. A mood that declares colours is
// deliberately breaking that tie, so it wins.
//
// Brightness is hard-capped here — the power governor's citizen for the ring.
#include "sdkconfig.h"

#include "bus.h"
#include "expressions.h"
#include "face_model.h"
#include "mood_model.h"

#include <cmath>

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
volatile uint8_t s_r = 0, s_g = 205, s_b = 255;  // follows the expression
volatile int s_mood = 0;                          // index into moods()

// Where the mood is in its cycle, 0..1. Kept across frames so changing mood
// does not restart the phase and make the ring stutter.
float s_phase = 0.0f;

Rgb face_color() { return Rgb{s_r, s_g, s_b}; }

// Colour for this instant. One stop is flat; several are walked around the
// cycle so a two-colour breathe drifts between them instead of blinking.
Rgb color_at(const Mood& m, float t) {
  const size_t n = m.colors.size();
  if (n == 0) return face_color();
  if (n == 1) return m.colors[0];
  const float x = t * static_cast<float>(n);
  const size_t i = static_cast<size_t>(x) % n;
  const size_t j = (i + 1) % n;
  const float f = x - std::floor(x);
  Rgb o;
  o.r = static_cast<uint8_t>(m.colors[i].r + (m.colors[j].r - m.colors[i].r) * f);
  o.g = static_cast<uint8_t>(m.colors[i].g + (m.colors[j].g - m.colors[i].g) * f);
  o.b = static_cast<uint8_t>(m.colors[i].b + (m.colors[j].b - m.colors[i].b) * f);
  return o;
}

void set_all(Rgb c, float bright) {
  const uint8_t r = static_cast<uint8_t>(c.r * bright * kMaxBright);
  const uint8_t g = static_cast<uint8_t>(c.g * bright * kMaxBright);
  const uint8_t b = static_cast<uint8_t>(c.b * bright * kMaxBright);
  for (int i = 0; i < s_count; i++) led_strip_set_pixel(s_strip, i, r, g, b);
  led_strip_refresh(s_strip);
}

// Comet with a fading tail, travelling `dir` around the ring.
void draw_spin(Rgb c, float t, int dir) {
  const float pos = (dir >= 0 ? t : 1.0f - t) * static_cast<float>(s_count);
  for (int i = 0; i < s_count; i++) {
    float d = std::fabs(static_cast<float>(i) - pos);
    if (d > s_count - d) d = s_count - d;  // wrap distance
    const float tail = 1.0f - d * 0.55f;
    const float br = (tail > 0 ? tail : 0) * kMaxBright;
    led_strip_set_pixel(s_strip, i, static_cast<uint8_t>(c.r * br),
                        static_cast<uint8_t>(c.g * br),
                        static_cast<uint8_t>(c.b * br));
  }
  led_strip_refresh(s_strip);
}

void ring_task(void*) {
  for (;;) {
    const int idx = s_mood;
    const Mood& m = moods()[idx >= 0 && idx < mood_count() ? idx : 0];

    if (m.period_ms > 0) {
      s_phase += static_cast<float>(kFrameMs) / static_cast<float>(m.period_ms);
      if (s_phase >= 1.0f) s_phase -= std::floor(s_phase);
    }
    const Rgb c = color_at(m, s_phase);

    switch (m.anim) {
      case Anim::Off:
        led_strip_clear(s_strip);
        break;
      case Anim::Solid:
        set_all(c, 1.0f);
        break;
      case Anim::Spin:
        draw_spin(c, s_phase, m.dir);
        break;
      case Anim::Pulse: {
        // Fast attack, exponential decay — a heartbeat rather than a sigh.
        // This is what makes Pulse read differently from Breathe at the same
        // period; a second sine would just be Breathe with another name.
        const float k = s_phase < 0.12f ? s_phase / 0.12f
                                        : std::exp(-(s_phase - 0.12f) * 4.5f);
        set_all(c, m.floor + k * (1.0f - m.floor));
        break;
      }
      case Anim::Breathe:
      default: {
        const float s = (std::sin(s_phase * 2 * 3.14159265f) + 1) / 2;  // 0..1
        set_all(c, m.floor + s * (1.0f - m.floor));
        break;
      }
    }
    vTaskDelay(pdMS_TO_TICKS(kFrameMs));
  }
}

void select_mood(const char* name) {
  const int i = mood_index(name);
  if (i >= 0) s_mood = i;
  else ESP_LOGW(TAG, "no mood \"%s\" in this pack — leaving %s", name, moods()[s_mood].name.c_str());
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

  // The expression carries the ring colour, and may also name a mood. That
  // name is a DEFAULT: a led.mood published afterwards still wins, which is
  // why the existing reflexes keep behaving exactly as they did.
  bus().subscribe("face.emotion", [](const Event& ev) {
    const int i = emotion_index(ev.payload.c_str());
    if (i < 0) return;
    const Emotion& e = emotions()[i];
    s_r = e.r; s_g = e.g; s_b = e.b;
    if (!e.mood.empty()) select_mood(e.mood.c_str());
  });
  bus().subscribe("led.mood", [](const Event& ev) { select_mood(ev.payload.c_str()); });

  ESP_LOGI(TAG, "WS2812 ring: %d LEDs on GPIO %d, %d moods", s_count,
           CONFIG_BUDDY_WS2812_PIN, mood_count());
  xTaskCreatePinnedToCore(ring_task, "ring", 3072, nullptr, 3, nullptr, 1);
}

}  // namespace buddy
