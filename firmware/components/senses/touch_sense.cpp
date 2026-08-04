// Petting pad via the capacitive touch peripheral — ESP-IDF v6 driver
// (esp_driver_touch_sens) on the ESP32-S3 (touch hw v2).
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

// GPIO → touch channel id. On the S3, channel n == GPIO n for GPIO 1..14.
int channel_for_gpio(int gpio) {
  return (gpio >= 1 && gpio <= 14) ? gpio : -1;
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
  // On touch hw v2 a touch RAISES the reading (it LOWERS it on the classic
  // ESP32 — opposite polarity, and the bug that ate an evening once). S3
  // deltas are large (~30%+), so a 15% band is both sensitive and noise-safe;
  // the two-sample confirm below guards the rest.
  const uint32_t threshold = baseline * 115 / 100;  // 15% above baseline
  ESP_LOGI(TAG, "baseline=%u threshold=%u (touch raises)", (unsigned)baseline,
           (unsigned)threshold);

  bool touching = false;
  int confirm = 0;
  int tick = 0;
  int64_t touch_start_ms = 0;
  for (;;) {
    const uint32_t v = read_smooth();
    const bool raw = v > threshold;
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
      TOUCH_SENSOR_V2_DEFAULT_SAMPLE_CONFIG(500, TOUCH_VOLT_LIM_L_0V5, TOUCH_VOLT_LIM_H_2V2),
  };
  touch_sensor_config_t sens_cfg = TOUCH_SENSOR_DEFAULT_BASIC_CONFIG(TOUCH_SAMPLE_CFG_NUM, sample_cfg);
  touch_sensor_handle_t sens = nullptr;
  ESP_ERROR_CHECK(touch_sensor_new_controller(&sens_cfg, &sens));

  touch_channel_config_t chan_cfg = {};
  chan_cfg.active_thresh[0] = 1;  // we do our own thresholding in the task
  chan_cfg.charge_speed = TOUCH_CHARGE_SPEED_7;
  chan_cfg.init_charge_volt = TOUCH_INIT_CHARGE_VOLT_DEFAULT;
  ESP_ERROR_CHECK(touch_sensor_new_channel(sens, chan_id, &chan_cfg, &s_chan));

  touch_sensor_filter_config_t filter_cfg = TOUCH_SENSOR_DEFAULT_FILTER_CONFIG();
  ESP_ERROR_CHECK(touch_sensor_config_filter(sens, &filter_cfg));
  ESP_ERROR_CHECK(touch_sensor_enable(sens));
  ESP_ERROR_CHECK(touch_sensor_start_continuous_scanning(sens));

  xTaskCreatePinnedToCore(touch_task, "touch", 3072, nullptr, 4, nullptr, 1);
}

}  // namespace buddy
