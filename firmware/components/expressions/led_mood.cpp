// Single PWM mood LED backend — breathing brightness, mono (no color).
// The Buddy Zero PoC indicator. Compiled only when selected in menuconfig.
// Doubles as the power governor's smallest citizen — brightness capped here.
#include "sdkconfig.h"
#if CONFIG_BUDDY_LED_PWM

#include "bus.h"
#include "expressions.h"

#include <cmath>
#include <string>

#include "driver/ledc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace buddy {
namespace {

constexpr int kMaxDuty = 200;  // of 255 — the governor's cap

struct Mood { const char* name; int period_ms; int floor; };
constexpr Mood kMoods[] = {
    {"calm",     3200, 10},
    {"excited",  700,  40},
    {"thinking", 1400, 5},
    {"off",      0,    0},
};
volatile int s_mood = 0;

void led_task(void*) {
  float phase = 0;
  for (;;) {
    const Mood& m = kMoods[s_mood];
    int duty = 0;
    if (m.period_ms > 0) {
      phase += 40.0f / m.period_ms;
      const float s = (sinf(phase * 2 * 3.14159265f) + 1) / 2;  // 0..1 breathe
      duty = m.floor + static_cast<int>(s * (kMaxDuty - m.floor));
    }
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
    vTaskDelay(pdMS_TO_TICKS(40));
  }
}

}  // namespace

void led_start() {
  ledc_timer_config_t timer = {};
  timer.speed_mode = LEDC_LOW_SPEED_MODE;
  timer.duty_resolution = LEDC_TIMER_8_BIT;
  timer.timer_num = LEDC_TIMER_0;
  timer.freq_hz = 5000;
  timer.clk_cfg = LEDC_AUTO_CLK;
  ESP_ERROR_CHECK(ledc_timer_config(&timer));

  ledc_channel_config_t ch = {};
  ch.gpio_num = CONFIG_BUDDY_PIN_LED;
  ch.speed_mode = LEDC_LOW_SPEED_MODE;
  ch.channel = LEDC_CHANNEL_0;
  ch.timer_sel = LEDC_TIMER_0;
  ch.duty = 0;
  ESP_ERROR_CHECK(ledc_channel_config(&ch));

  bus().subscribe("led.mood", [](const Event& ev) {
    for (size_t i = 0; i < sizeof kMoods / sizeof kMoods[0]; i++)
      if (ev.payload == kMoods[i].name) { s_mood = static_cast<int>(i); return; }
  });

  xTaskCreatePinnedToCore(led_task, "led", 2048, nullptr, 3, nullptr, 1);
}

}  // namespace buddy

#endif  // CONFIG_BUDDY_LED_PWM
