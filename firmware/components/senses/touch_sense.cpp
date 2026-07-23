// Petting pad via the capacitive touch peripheral — ESP-IDF v6 driver
// (esp_driver_touch_sens). Works on classic ESP32 (hw v1, DevKit V1 PoC)
// and ESP32-S3 (hw v2, the real buddy) via the version macros.
#include "bus.h"
#include "senses.h"

#include "driver/touch_sens.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "soc/soc_caps.h"

static const char* TAG = "touch";

namespace buddy {
namespace {

touch_channel_handle_t s_chan = nullptr;

// GPIO → touch channel id.
int channel_for_gpio(int gpio) {
#if SOC_TOUCH_SENSOR_VERSION == 1  // classic ESP32: T0=GPIO4 … T9=GPIO32
  switch (gpio) {
    case 4:  return 0; case 0:  return 1; case 2:  return 2; case 15: return 3;
    case 13: return 4; case 12: return 5; case 14: return 6; case 27: return 7;
    case 33: return 8; case 32: return 9;
    default: return -1;
  }
#else  // ESP32-S3 (hw v2): touch channel n == GPIO n, for GPIO 1..14
  return (gpio >= 1 && gpio <= 14) ? gpio : -1;
#endif
}

uint32_t read_smooth() {
  uint32_t v[TOUCH_SAMPLE_CFG_NUM] = {0};
  touch_channel_read_data(s_chan, TOUCH_CHAN_DATA_TYPE_SMOOTH, v);
  return v[0];
}

void touch_task(void*) {
  vTaskDelay(pdMS_TO_TICKS(500));  // let the filter settle

  uint32_t baseline = 0;
  for (int i = 0; i < 16; i++) {
    baseline += read_smooth();
    vTaskDelay(pdMS_TO_TICKS(20));
  }
  baseline /= 16;
  // Touch polarity flips between chip generations:
  //   hw v1 (classic ESP32): a touch LOWERS the reading  → fire below threshold
  //   hw v2 (ESP32-S3):      a touch RAISES the reading   → fire above threshold
  // A bare wire only shifts a few %, but S3 deltas are large (~30%+), so a
  // 15% band is both sensitive and noise-safe; two-sample confirm below guards.
#if SOC_TOUCH_SENSOR_VERSION == 1
  const uint32_t threshold = baseline * 9 / 10;    // 10% below baseline
  const bool touch_raises = false;
#else
  const uint32_t threshold = baseline * 115 / 100;  // 15% above baseline
  const bool touch_raises = true;
#endif
  ESP_LOGI(TAG, "baseline=%u threshold=%u (touch %s)", (unsigned)baseline,
           (unsigned)threshold, touch_raises ? "raises" : "lowers");

  bool touching = false;
  int confirm = 0;
  int tick = 0;
  int64_t touch_start_ms = 0;
  for (;;) {
    const uint32_t v = read_smooth();
    const bool raw = touch_raises ? (v > threshold) : (v < threshold);
    const int64_t ms = esp_log_timestamp();

#if CONFIG_BUDDY_DEBUG
    if (++tick % 40 == 0)  // every ~2 s: watch these while touching the wire
      ESP_LOGI(TAG, "raw=%u baseline=%u threshold=%u touching=%d",
               (unsigned)v, (unsigned)baseline, (unsigned)threshold, touching);
#endif

    if (raw == touching) {
      confirm = 0;
    } else if (++confirm >= 2) {  // two consecutive samples to switch state
      confirm = 0;
      touching = raw;
      if (touching) {
        touch_start_ms = ms;
        bus().publish("touch.down", "pad0");
      } else {
        bus().publish(ms - touch_start_ms < 400 ? "touch.poke" : "touch.pet", "pad0");
      }
    }
    vTaskDelay(pdMS_TO_TICKS(25));  // ~50 ms to confirmed state change
  }
}

}  // namespace

void touch_sense_start(int gpio_touch_pad) {
  const int chan_id = channel_for_gpio(gpio_touch_pad);
  if (chan_id < 0) {
    ESP_LOGE(TAG, "GPIO %d is not touch-capable", gpio_touch_pad);
    return;
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
  ESP_ERROR_CHECK(touch_sensor_new_controller(&sens_cfg, &sens));

  touch_channel_config_t chan_cfg = {};
#if SOC_TOUCH_SENSOR_VERSION == 1
  chan_cfg.abs_active_thresh[0] = 1;  // we do our own thresholding in the task
  chan_cfg.charge_speed = TOUCH_CHARGE_SPEED_7;
  chan_cfg.init_charge_volt = TOUCH_INIT_CHARGE_VOLT_DEFAULT;
  chan_cfg.group = TOUCH_CHAN_TRIG_GROUP_BOTH;
#else
  chan_cfg.active_thresh[0] = 1;
  chan_cfg.charge_speed = TOUCH_CHARGE_SPEED_7;
  chan_cfg.init_charge_volt = TOUCH_INIT_CHARGE_VOLT_DEFAULT;
#endif
  ESP_ERROR_CHECK(touch_sensor_new_channel(sens, chan_id, &chan_cfg, &s_chan));

  touch_sensor_filter_config_t filter_cfg = TOUCH_SENSOR_DEFAULT_FILTER_CONFIG();
  ESP_ERROR_CHECK(touch_sensor_config_filter(sens, &filter_cfg));
  ESP_ERROR_CHECK(touch_sensor_enable(sens));
  ESP_ERROR_CHECK(touch_sensor_start_continuous_scanning(sens));

  xTaskCreatePinnedToCore(touch_task, "touch", 3072, nullptr, 4, nullptr, 1);
}

}  // namespace buddy
