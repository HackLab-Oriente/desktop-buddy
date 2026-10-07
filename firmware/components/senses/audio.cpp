#include "senses.h"
#include "bus.h"
#include "esp_log.h"
#include "driver/i2s_std.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cmath>

static const char* TAG = "audio_sense";

namespace buddy {
namespace {

i2s_chan_handle_t rx_handle = nullptr;

void audio_task(void*) {
  int32_t buf[256];
  size_t bytes_read;
  bool is_loud = false;
  int quiet_count = 0;

  while (true) {
    if (i2s_channel_read(rx_handle, buf, sizeof(buf), &bytes_read, portMAX_DELAY) == ESP_OK) {
      int samples = bytes_read / sizeof(int32_t);
      if (samples > 0) {
        int64_t sum = 0;
        for (int i = 0; i < samples; i++) {
          sum += (buf[i] >> 16);
        }
        int32_t dc_offset = sum / samples;

        int64_t sum_sq = 0;
        for (int i = 0; i < samples; i++) {
          int32_t sample = (buf[i] >> 16) - dc_offset;
          sum_sq += (int64_t)sample * sample;
        }

        int rms = std::sqrt(sum_sq / samples);
        
        // Thresholds are arbitrary, needs tuning. A loud clap/voice is usually > 5000.
        // Let's use 2000 as a start, to be more sensitive.
        if (rms > 2000) {
          quiet_count = 0;
          if (!is_loud) {
            is_loud = true;
            bus().publish("audio.loud", "");
            ESP_LOGI(TAG, "loud! rms=%d (dc=%ld)", rms, (long)dc_offset);
          }
        } else {
          if (is_loud) {
            quiet_count++;
            if (quiet_count > 10) { // ~ 10 * (256/16000) = 160ms of quiet
              is_loud = false;
              bus().publish("audio.quiet", "");
              ESP_LOGI(TAG, "quiet");
            }
          } else {
            // Debug every ~2 seconds (16000/256 = 62.5 loops/sec)
            static int debug_tick = 0;
            if (++debug_tick >= 125) {
              debug_tick = 0;
              ESP_LOGI(TAG, "background rms=%d (dc=%ld)", rms, (long)dc_offset);
            }
          }
        }
      }
    }
  }
}

} // namespace

bool mic_start(const MicPins& pins) {
  i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  if (i2s_new_channel(&chan_cfg, nullptr, &rx_handle) != ESP_OK) {
    ESP_LOGE(TAG, "failed to create i2s rx channel");
    return false;
  }

  i2s_std_config_t std_cfg = {
      .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(16000),
      .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_MONO),
      .gpio_cfg = {
          .mclk = I2S_GPIO_UNUSED,
          .bclk = static_cast<gpio_num_t>(pins.bclk),
          .ws = static_cast<gpio_num_t>(pins.ws),
          .dout = I2S_GPIO_UNUSED,
          .din = static_cast<gpio_num_t>(pins.sd),
          .invert_flags = {
              .mclk_inv = false,
              .bclk_inv = false,
              .ws_inv = false,
          },
      },
  };
  
  std_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT;

  if (i2s_channel_init_std_mode(rx_handle, &std_cfg) != ESP_OK) {
    ESP_LOGE(TAG, "failed to init i2s std mode");
    return false;
  }

  if (i2s_channel_enable(rx_handle) != ESP_OK) {
    ESP_LOGE(TAG, "failed to enable i2s channel");
    return false;
  }

  ESP_LOGI(TAG, "I2S microphone started on BCLK=%d WS=%d SD=%d", pins.bclk, pins.ws, pins.sd);
  xTaskCreatePinnedToCore(audio_task, "audio_sense", 4096, nullptr, 4, nullptr, 1);
  return true;
}

} // namespace buddy
