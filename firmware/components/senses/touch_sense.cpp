// Petting pad via the capacitive touch peripheral — ESP-IDF v6 driver
// (esp_driver_touch_sens) on the ESP32-S3 (touch hw v2).
#include "bus.h"
#include "senses.h"

#include "driver/touch_sens.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "soc/soc_caps.h"

static const char* TAG = "touch";

namespace buddy {
namespace {

touch_channel_handle_t s_chan = nullptr;
const char* s_pad = "pad0";

// The identity mapping is an S3 fact. Assumed on the classic ESP32 it made the
// default GPIO 4 read channel 4 = T4 = GPIO13, the RC522 MOSI pin.
int channel_for_gpio(int gpio) {
#if SOC_TOUCH_SENSOR_VERSION == 1
  switch (gpio) {
    case 4:  return 0;
    case 0:  return 1;
    case 2:  return 2;
    case 15: return 3;
    case 13: return 4;
    case 12: return 5;
    case 14: return 6;
    case 27: return 7;
    case 33: return 8;
    case 32: return 9;
    default: return -1;
  }
#else
  return (gpio >= 1 && gpio <= 14) ? gpio : -1;   // S3: channel n == GPIO n
#endif
}

// False on read failure. Silently returning 0 gave a baseline of 0, a
// threshold of 0, and a permanent touch.
bool read_smooth(uint32_t& out) {
  uint32_t v[TOUCH_SAMPLE_CFG_NUM] = {0};
  if (touch_channel_read_data(s_chan, TOUCH_CHAN_DATA_TYPE_SMOOTH, v) != ESP_OK)
    return false;
  out = v[0];
  return true;
}

void touch_task(void*) {
  vTaskDelay(pdMS_TO_TICKS(500));  // let the filter settle

  uint64_t sum = 0;
  int got = 0;
  for (int i = 0; i < 16; i++) {
    uint32_t v = 0;
    if (read_smooth(v)) { sum += v; got++; }
    vTaskDelay(pdMS_TO_TICKS(20));
  }
  if (got == 0) {
    ESP_LOGE(TAG, "touch peripheral never read — pad disabled");
    vTaskDelete(nullptr);
    return;
  }
  uint32_t baseline = static_cast<uint32_t>(sum / got);
  if (baseline == 0 || baseline > 30u * 1000u * 1000u) {
    ESP_LOGE(TAG, "implausible touch baseline %u — pad disabled", (unsigned)baseline);
    vTaskDelete(nullptr);
    return;
  }
  // The two touch generations read in OPPOSITE directions: on hw v2 (S3) a
  // touch RAISES the reading, on v1 (classic ESP32) it LOWERS it. This cost us
  // an evening once, so it is a branch and not an assumption. S3 deltas are
  // large (~30%+), so a 15% band is both sensitive and noise-safe; the
  // two-sample confirm below guards the rest.
#if SOC_TOUCH_SENSOR_VERSION == 1
  uint32_t threshold = static_cast<uint32_t>(uint64_t(baseline) * 9 / 10);   // 10% BELOW
  const bool touch_raises = false;
#else
  uint32_t threshold = static_cast<uint32_t>(uint64_t(baseline) * 115 / 100);  // 15% above
  const bool touch_raises = true;
#endif
  ESP_LOGI(TAG, "baseline=%u threshold=%u (touch %s)", (unsigned)baseline,
           (unsigned)threshold, touch_raises ? "raises" : "lowers");

  bool touching = false;
  int confirm = 0;
#if CONFIG_BUDDY_DEBUG
  int tick = 0;
#endif
  int64_t touch_start_ms = 0;
  for (;;) {
    uint32_t v = 0;
    if (!read_smooth(v)) { vTaskDelay(pdMS_TO_TICKS(25)); continue; }
    const bool raw = touch_raises ? (v > threshold) : (v < threshold);
    // Not esp_log_timestamp(): 32-bit ms, wraps at 49.7 days, and a touch
    // across the wrap reads as a poke.
    const int64_t ms = esp_timer_get_time() / 1000;

#if CONFIG_BUDDY_DEBUG
    if (++tick % 40 == 0)  // every ~2 s: watch these while touching the wire
      ESP_LOGI(TAG, "raw=%u baseline=%u threshold=%u touching=%d",
               (unsigned)v, (unsigned)baseline, (unsigned)threshold, touching);
#endif

    if (raw == touching) {
      confirm = 0;
      // Booting with a finger on the pad calibrated against a touched reading
      // and left the pad dead for the session; thermal drift did the inverse.
      if (!touching) {
        if (v > baseline) baseline += (v - baseline + 63) / 64;
        else if (baseline > v) baseline -= (baseline - v + 63) / 64;
        if (baseline == 0) baseline = 1;
#if SOC_TOUCH_SENSOR_VERSION == 1
        threshold = static_cast<uint32_t>(uint64_t(baseline) * 9 / 10);
#else
        threshold = static_cast<uint32_t>(uint64_t(baseline) * 115 / 100);
#endif
      }
    } else if (++confirm >= 2) {  // two consecutive samples to switch state
      confirm = 0;
      touching = raw;
      if (touching) {
        touch_start_ms = ms;
        bus().publish("touch.down", s_pad);
      } else {
        bus().publish(ms - touch_start_ms < 400 ? "touch.poke" : "touch.pet", s_pad);
      }
    }
    vTaskDelay(pdMS_TO_TICKS(25));  // ~50 ms to confirmed state change
  }
}

}  // namespace

// Never ESP_ERROR_CHECK: an abort on the main task is a reboot loop, and a
// petting pad is optional hardware.
#define TRY(expr, what)                                            \
  do {                                                             \
    const esp_err_t _e = (expr);                                   \
    if (_e != ESP_OK) {                                            \
      ESP_LOGE(TAG, "%s: %s — no petting pad", what,               \
               esp_err_to_name(_e));                               \
      return false;                                                \
    }                                                              \
  } while (0)

bool touch_start(int gpio_touch_pad, const char* pad_name) {
  if (pad_name && *pad_name) s_pad = pad_name;
  const int chan_id = channel_for_gpio(gpio_touch_pad);
  if (chan_id < 0) {
    ESP_LOGE(TAG, "GPIO %d is not touch-capable on this target", gpio_touch_pad);
    return false;
  }

  static touch_sensor_sample_config_t sample_cfg[TOUCH_SAMPLE_CFG_NUM] = {
#if SOC_TOUCH_SENSOR_VERSION == 1
      TOUCH_SENSOR_V1_DEFAULT_SAMPLE_CONFIG(5.0, TOUCH_VOLT_LIM_L_0V5, TOUCH_VOLT_LIM_H_1V7),
#elif SOC_TOUCH_SENSOR_VERSION == 2
      TOUCH_SENSOR_V2_DEFAULT_SAMPLE_CONFIG(500, TOUCH_VOLT_LIM_L_0V5, TOUCH_VOLT_LIM_H_2V2),
#else
#error "unsupported touch hw version"
#endif
  };
  touch_sensor_config_t sens_cfg = TOUCH_SENSOR_DEFAULT_BASIC_CONFIG(TOUCH_SAMPLE_CFG_NUM, sample_cfg);
  touch_sensor_handle_t sens = nullptr;
  TRY(touch_sensor_new_controller(&sens_cfg, &sens), "touch controller");

  touch_channel_config_t chan_cfg = {};
#if SOC_TOUCH_SENSOR_VERSION == 1
  chan_cfg.abs_active_thresh[0] = 1;  // we do our own thresholding in the task
  chan_cfg.charge_speed = TOUCH_CHARGE_SPEED_7;
  chan_cfg.init_charge_volt = TOUCH_INIT_CHARGE_VOLT_DEFAULT;
  chan_cfg.group = TOUCH_CHAN_TRIG_GROUP_BOTH;
#else
  chan_cfg.active_thresh[0] = 1;  // we do our own thresholding in the task
  chan_cfg.charge_speed = TOUCH_CHARGE_SPEED_7;
  chan_cfg.init_charge_volt = TOUCH_INIT_CHARGE_VOLT_DEFAULT;
#endif
  TRY(touch_sensor_new_channel(sens, chan_id, &chan_cfg, &s_chan), "touch channel");

  touch_sensor_filter_config_t filter_cfg = TOUCH_SENSOR_DEFAULT_FILTER_CONFIG();
  TRY(touch_sensor_config_filter(sens, &filter_cfg), "touch filter");
  TRY(touch_sensor_enable(sens), "touch enable");
  TRY(touch_sensor_start_continuous_scanning(sens), "touch scan");

  xTaskCreatePinnedToCore(touch_task, "touch", 3072, nullptr, 4, nullptr, 1);
  return true;
}
#undef TRY

}  // namespace buddy
