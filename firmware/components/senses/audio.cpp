#include "senses.h"
#include "bus.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "driver/gpio.h"
#include "driver/i2s_std.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cmath>
#include <cstring>

static const char* TAG = "audio_sense";

namespace buddy {
namespace {

#define RATE 16000
#define SLOT_BITS I2S_DATA_BIT_WIDTH_32BIT
#define CHUNK 512
#define MAX_SECONDS 30
#define MAX_FRAMES (RATE * MAX_SECONDS)
#define FULL_SCALE 8388608.0f           // 24-bit

#define TARGET_DBFS   -2.0f
#define MAX_GAIN_DB   30.0f
#define HIGHPASS_HZ   400.0f
#define AB_COMPARE    0

static i2s_chan_handle_t s_rx = nullptr, s_tx = nullptr;
static float s_peak = 0;
static int16_t *s_rec = nullptr;
static int32_t *s_chunk = nullptr;

static AudioPins s_pins;

static inline int32_t to_24bit(int32_t raw) { return raw >> 8; }
static float dbfs(float v) { return v > 1.0f ? 20.0f * std::log10(v / FULL_SCALE) : -120.0f; }

static void amp_enabled(bool on) { gpio_set_level((gpio_num_t)s_pins.mute, on ? 1 : 0); }
static bool ptt_held(void) { return gpio_get_level((gpio_num_t)s_pins.ptt) == 0; }

static void i2s_open_rx(void) {
  i2s_chan_config_t cc = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  ESP_ERROR_CHECK(i2s_new_channel(&cc, NULL, &s_rx));
  i2s_std_config_t cfg = {
      .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(RATE),
      .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(SLOT_BITS, I2S_SLOT_MODE_MONO),
      .gpio_cfg = {
          .mclk = I2S_GPIO_UNUSED, .bclk = (gpio_num_t)s_pins.bclk, .ws = (gpio_num_t)s_pins.ws,
          .dout = I2S_GPIO_UNUSED, .din = (gpio_num_t)s_pins.din,
          .invert_flags = {false,false,false}
      },
  };
  cfg.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT;
  ESP_ERROR_CHECK(i2s_channel_init_std_mode(s_rx, &cfg));
  ESP_ERROR_CHECK(i2s_channel_enable(s_rx));
}

static void i2s_open_tx(void) {
  i2s_chan_config_t cc = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  ESP_ERROR_CHECK(i2s_new_channel(&cc, &s_tx, NULL));
  i2s_std_config_t cfg = {
      .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(RATE),
      .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(SLOT_BITS, I2S_SLOT_MODE_STEREO),
      .gpio_cfg = {
          .mclk = I2S_GPIO_UNUSED, .bclk = (gpio_num_t)s_pins.bclk, .ws = (gpio_num_t)s_pins.ws,
          .dout = (gpio_num_t)s_pins.dout, .din = I2S_GPIO_UNUSED,
          .invert_flags = {false,false,false}
      },
  };
  ESP_ERROR_CHECK(i2s_channel_init_std_mode(s_tx, &cfg));
  ESP_ERROR_CHECK(i2s_channel_enable(s_tx));
}

static void i2s_close(i2s_chan_handle_t *c) {
  if (*c) {
    i2s_channel_disable(*c);
    i2s_del_channel(*c);
    *c = NULL;
  }
}

static size_t record(void) {
  ESP_LOGW(TAG, "recording — speaker is hard-muted");
  size_t frames = 0;
  float peak = 0;
  double sumsq = 0;
  int clipped = 0;

  while (ptt_held() && frames < MAX_FRAMES) {
    size_t got = 0;
    if (i2s_channel_read(s_rx, s_chunk, CHUNK * sizeof(int32_t), &got, 200) != ESP_OK)
      continue;
    const int n = got / sizeof(int32_t);
    for (int i = 0; i < n && frames < MAX_FRAMES; i++) {
      const int32_t s = to_24bit(s_chunk[i]);
      const float a = std::fabs((float)s);
      if (a > peak) peak = a;
      if (a >= FULL_SCALE - 2) clipped++;
      sumsq += (double)s * s;
      s_rec[frames++] = (int16_t)(s >> 8);
    }
  }

  s_peak = peak;
  const float rms = frames ? std::sqrt((float)(sumsq / frames)) : 0;
  ESP_LOGI(TAG, "captured %.2f s — peak %.1f dBFS, rms %.1f dBFS, %d clipped",
           (double)frames / RATE, (double)dbfs(peak), (double)dbfs(rms), clipped);
  return frames;
}

static float buffer_peak(size_t frames) {
  float peak = 0;
  for (size_t i = 0; i < frames; i++) {
    const float a = std::fabs((float)s_rec[i]);
    if (a > peak) peak = a;
  }
  return peak;
}

static void highpass(size_t frames) {
  if (HIGHPASS_HZ <= 0) return;
  const float rc = 1.0f / (2.0f * (float)M_PI * HIGHPASS_HZ);
  const float dt = 1.0f / RATE;
  const float a = rc / (rc + dt);
  float y = 0, x_prev = 0;
  for (size_t i = 0; i < frames; i++) {
    const float x = (float)s_rec[i];
    y = a * (y + x - x_prev);
    x_prev = x;
    s_rec[i] = (int16_t)(y < -32768.0f ? -32768.0f : (y > 32767.0f ? 32767.0f : y));
  }
}

static bool playback(size_t frames, const char *label) {
  const float peak = buffer_peak(frames);
  const float target = 32767.0f * std::pow(10.0f, TARGET_DBFS / 20.0f);
  const float ceiling = std::pow(10.0f, MAX_GAIN_DB / 20.0f);
  float gain = (peak > 8.0f) ? target / peak : 1.0f;
  if (gain < 1.0f) gain = 1.0f;
  if (gain > ceiling) gain = ceiling;
  ESP_LOGI(TAG, "  %s: %.2f s, %+.1f dB gain", label, (double)frames / RATE, (double)(20.0f * std::log10(gain)));
  amp_enabled(true);
  vTaskDelay(pdMS_TO_TICKS(20));

  for (size_t i = 0; i < frames; i += CHUNK / 2) {
    const size_t n = (frames - i < CHUNK / 2) ? frames - i : CHUNK / 2;
    for (size_t j = 0; j < n; j++) {
      int32_t v = (int32_t)(s_rec[i + j] * gain);
      if (v > 32767) v = 32767; else if (v < -32768) v = -32768;
      const int32_t s = v << 16;
      s_chunk[j * 2] = s;
      s_chunk[j * 2 + 1] = s;
    }
    if (ptt_held()) {
      amp_enabled(false);
      ESP_LOGW(TAG, "cut off — you pressed the button, listening again");
      return true;
    }
    size_t wrote = 0;
    i2s_channel_write(s_tx, s_chunk, n * 2 * sizeof(int32_t), &wrote, 1000);
  }
  vTaskDelay(pdMS_TO_TICKS(40));
  amp_enabled(false);
  return false;
}

void audio_task(void*) {
  gpio_config_t mute = {};
  mute.pin_bit_mask = 1ULL << s_pins.mute;
  mute.mode = GPIO_MODE_OUTPUT;
  mute.pull_up_en = GPIO_PULLUP_DISABLE;
  mute.pull_down_en = GPIO_PULLDOWN_DISABLE;
  mute.intr_type = GPIO_INTR_DISABLE;
  ESP_ERROR_CHECK(gpio_config(&mute));
  amp_enabled(false);

  gpio_config_t btn = {};
  btn.pin_bit_mask = 1ULL << s_pins.ptt;
  btn.mode = GPIO_MODE_INPUT;
  btn.pull_up_en = GPIO_PULLUP_ENABLE;
  btn.pull_down_en = GPIO_PULLDOWN_DISABLE;
  btn.intr_type = GPIO_INTR_DISABLE;
  ESP_ERROR_CHECK(gpio_config(&btn));

  s_rec = (int16_t*)heap_caps_malloc(MAX_FRAMES * sizeof(int16_t), MALLOC_CAP_SPIRAM);
  s_chunk = (int32_t*)heap_caps_malloc(CHUNK * sizeof(int32_t), MALLOC_CAP_DMA);
  if (!s_rec || !s_chunk) {
    ESP_LOGE(TAG, "no memory — is PSRAM enabled? (%d KB wanted)", (int)(MAX_FRAMES * sizeof(int16_t) / 1024));
    return;
  }

  i2s_open_rx();
  ESP_LOGI(TAG, "Audio started: BCLK=%d WS=%d DIN=%d DOUT=%d MUTE=%d PTT=%d", s_pins.bclk, s_pins.ws, s_pins.din, s_pins.dout, s_pins.mute, s_pins.ptt);

  bool is_loud = false;
  int quiet_count = 0;

  for (;;) {
    if (ptt_held()) {
      vTaskDelay(pdMS_TO_TICKS(30));
      if (!ptt_held()) continue;

      const size_t frames = record();
      i2s_close(&s_rx);
      bool cut = false;
      if (frames > RATE / 10) {
        i2s_open_tx();
        if (AB_COMPARE) {
          cut = playback(frames, "A sin filtrar");
          if (!cut) vTaskDelay(pdMS_TO_TICKS(400));
        }
        if (!cut) {
          highpass(frames);
          cut = playback(frames, AB_COMPARE ? "B pasa-altos" : "reproduciendo");
        }
        i2s_close(&s_tx);
      }
      i2s_open_rx();
      if (!cut) while (ptt_held()) vTaskDelay(pdMS_TO_TICKS(20));
      continue;
    }

    size_t got = 0;
    if (i2s_channel_read(s_rx, s_chunk, CHUNK * sizeof(int32_t), &got, 100) != ESP_OK) continue;
    const int n = got / sizeof(int32_t);
    int64_t chunk_sum = 0;
    
    // DC offset detection to be completely equivalent to previous logic
    for (int i = 0; i < n; i++) chunk_sum += (s_chunk[i] >> 16);
    int32_t dc_offset = chunk_sum / n;

    int64_t sum_sq_16 = 0;
    for (int i = 0; i < n; i++) {
      int32_t sample = (s_chunk[i] >> 16) - dc_offset;
      sum_sq_16 += (int64_t)sample * sample;
    }
    
    int rms_current = std::sqrt(sum_sq_16 / n); 
    if (rms_current > 2000) { 
      quiet_count = 0;
      if (!is_loud) {
        is_loud = true;
        bus().publish("audio.loud", "");
      }
    } else {
      if (is_loud) {
        quiet_count++;
        if (quiet_count > 10) {
          is_loud = false;
          bus().publish("audio.quiet", "");
        }
      }
    }
  }
}

} // namespace

bool audio_start(const AudioPins& pins) {
  s_pins = pins;
  xTaskCreatePinnedToCore(audio_task, "audio_sense", 8192, nullptr, 4, nullptr, 1);
  return true;
}

} // namespace buddy
