// Spike: does sound come out of the MAX98357A on the pins we planned?
//
// Isolated on purpose — this is the whole app. When I2S goes into the real
// firmware, a failure is ambiguous (bus? task priority? PSRAM?); here it can
// only be the wiring or the I2S config.
//
// Right now it plays ONE continuous A (440 Hz) and nothing else. That is the
// sharpest possible test of audio quality: a pure sine has no transients to
// hide behind, so buzz, rasp, hum or crackle are all obvious, and any of them
// points at the hardware rather than at the code.
//
// The trick that makes it exact: at 16 kHz, 440 Hz is 36.3636... samples per
// cycle, which does not divide evenly — but ELEVEN cycles are exactly 400
// samples (440 * 400 = 11 * 16000). So one 400-frame buffer holds a whole
// number of periods and can be written forever, seamlessly. No phase
// accumulator, therefore no drift over hours, and no discontinuity at the
// loop seam. Rebuilding the wave every buffer from a float phase would give a
// tiny error at every seam, and periodic tiny errors are exactly what a
// listener hears as a buzz.
#include <math.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/i2s_std.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "i2s-out";

// Proposed in hardware/buddy-s3-audio.md. Change here first, then there.
#define PIN_BCLK 15
#define PIN_WS   16
#define PIN_DOUT 18
#define PIN_MUTE 2      // MAX98357A SD: low = output stage off, high = on

#define SAMPLE_RATE 16000
#define TONE_HZ     440
#define PERIOD_FRAMES 400      // = 11 cycles of 440 Hz at 16 kHz, exactly
#define PEAK        7000       // of 32767 — raise once it sounds right

static i2s_chan_handle_t s_tx;
static int16_t s_wave[PERIOD_FRAMES * 2];    // stereo, both slots identical

static void amp_enabled(bool on) { gpio_set_level(PIN_MUTE, on ? 1 : 0); }

static void write_wave(float gain) {
  int16_t buf[PERIOD_FRAMES * 2];
  const int16_t *src = s_wave;
  if (gain < 1.0f) {
    for (int i = 0; i < PERIOD_FRAMES * 2; i++) buf[i] = (int16_t)(s_wave[i] * gain);
    src = buf;
  }
  size_t written = 0;
  i2s_channel_write(s_tx, src, sizeof s_wave, &written, 1000);
}

void app_main(void) {
  // One buffer, one whole number of periods, computed once.
  for (int i = 0; i < PERIOD_FRAMES; i++) {
    const float phase = 2.0f * (float)M_PI * TONE_HZ * i / SAMPLE_RATE;
    const int16_t s = (int16_t)(sinf(phase) * PEAK);
    s_wave[i * 2] = s;
    s_wave[i * 2 + 1] = s;                   // both slots: the SD-pin channel
  }                                          // select cannot pick a silent one

  gpio_config_t mute = {.pin_bit_mask = 1ULL << PIN_MUTE, .mode = GPIO_MODE_OUTPUT};
  ESP_ERROR_CHECK(gpio_config(&mute));
  amp_enabled(false);                        // quiet until the clock is up

  i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
  ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &s_tx, NULL));

  i2s_std_config_t std_cfg = {
      .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
      .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                      I2S_SLOT_MODE_STEREO),
      .gpio_cfg = {
          .mclk = I2S_GPIO_UNUSED,           // the MAX98357A makes its own
          .bclk = PIN_BCLK,
          .ws = PIN_WS,
          .dout = PIN_DOUT,
          .din = I2S_GPIO_UNUSED,
      },
  };
  ESP_ERROR_CHECK(i2s_channel_init_std_mode(s_tx, &std_cfg));
  ESP_ERROR_CHECK(i2s_channel_enable(s_tx));

  ESP_LOGI(TAG, "i2s up: bclk=%d ws=%d dout=%d, mute=%d, %d Hz 16-bit stereo",
           PIN_BCLK, PIN_WS, PIN_DOUT, PIN_MUTE, SAMPLE_RATE);
  ESP_LOGI(TAG, "continuous %d Hz A, %d/32767 peak, %d frames = 11 exact cycles",
           TONE_HZ, PEAK, PERIOD_FRAMES);
  ESP_LOGW(TAG, "a pure sine should sound like a tuning fork. Buzz or rasp => "
                "suspect BCLK/LRC swapped (15 <-> 16). Thin and quiet => Vin "
                "is on 3V3, not 5V. A hum under the note => power, not data.");

  // Bring the amp up on a running clock, then ramp in over ~250 ms so it
  // starts without a thump. After this the tone never stops.
  for (int i = 0; i < 4; i++) write_wave(0.0f);
  amp_enabled(true);
  for (int i = 0; i < 10; i++) write_wave((float)(i + 1) / 10.0f);

  int64_t frames = 0;
  int next_log = 10;
  for (;;) {
    write_wave(1.0f);
    frames += PERIOD_FRAMES;
    const int secs = (int)(frames / SAMPLE_RATE);
    if (secs >= next_log) {                  // heartbeat, so a silent speaker
      next_log += 10;                        // can be told from a dead board
      ESP_LOGI(TAG, "still playing — %d s", secs);
    }
  }
}
