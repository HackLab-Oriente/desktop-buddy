// Spike: does sound come out of the MAX98357A on the pins we planned?
//
// Isolated on purpose — this is the whole app. When I2S goes into the real
// firmware, a failure is ambiguous (bus? task priority? PSRAM?); here it can
// only be the wiring or the I2S config.
//
// The first version of this played raw tones and a linear sweep, and it
// sounded like a car alarm. Two reasons, both fixed here:
//
//   * NO ENVELOPE. A note that starts and ends at full amplitude is a step
//     discontinuity, and a speaker reproduces a step as a click. Every note
//     now fades in over 8 ms and out over 25 ms with a raised cosine, which
//     is short enough to still feel instant and long enough to remove the
//     click entirely. This is most of what "clean" means here.
//   * LINEAR SWEEP. Pitch is perceived logarithmically, so a linear ramp
//     rushes the low end and crawls through the high end. Sweeping
//     exponentially sounds like a slide instead of an alarm.
//
// So this now plays chirps rather than test tones — which is also what the
// buddy actually needs, since its voice is chirps first and speech later.
// Treat the sequence below as a first sketch of that vocabulary.
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
#define CHUNK       256
#define PEAK        7000.0f    // of 32767 — deliberately not loud

static i2s_chan_handle_t s_tx;

static void amp_enabled(bool on) { gpio_set_level(PIN_MUTE, on ? 1 : 0); }

static void write_frames(const int16_t *buf, int frames) {
  size_t written = 0;
  i2s_channel_write(s_tx, buf, (size_t)frames * 2 * sizeof(int16_t), &written, 1000);
}

// One chirp: an exponential glide from f0 to f1, with a raised-cosine fade at
// each end so it neither clicks on nor clicks off. f0 == f1 gives a steady
// note. `gain` is a fraction of PEAK.
static void chirp(float f0, float f1, int ms, float gain) {
  const int total = SAMPLE_RATE * ms / 1000;
  int attack = SAMPLE_RATE * 8 / 1000;
  int release = SAMPLE_RATE * 25 / 1000;
  if (attack + release > total) {            // very short notes: split evenly
    attack = total / 3;
    release = total / 3;
  }
  const float ratio = f1 / f0;
  float phase = 0.0f;                        // per-note: each starts at zero
  int16_t buf[CHUNK * 2];                    // crossing, so no click either

  for (int done = 0; done < total; done += CHUNK) {
    const int n = (total - done < CHUNK) ? (total - done) : CHUNK;
    for (int i = 0; i < n; i++) {
      const int k = done + i;
      const float t = (float)k / (float)total;
      const float freq = f0 * powf(ratio, t);          // exponential glide
      phase += 2.0f * (float)M_PI * freq / SAMPLE_RATE;
      if (phase > 2.0f * (float)M_PI) phase -= 2.0f * (float)M_PI;

      float env = 1.0f;
      if (k < attack) {
        env = 0.5f * (1.0f - cosf((float)M_PI * k / attack));
      } else if (k > total - release) {
        env = 0.5f * (1.0f - cosf((float)M_PI * (total - k) / release));
      }

      const int16_t s = (int16_t)(sinf(phase) * PEAK * gain * env);
      buf[i * 2] = s;
      buf[i * 2 + 1] = s;                    // both slots: the SD-pin channel
    }                                        // select cannot pick a silent one
    write_frames(buf, n);
  }
}

static void rest(int ms) {
  int16_t buf[CHUNK * 2];
  memset(buf, 0, sizeof buf);
  const int total = SAMPLE_RATE * ms / 1000;
  for (int done = 0; done < total; done += CHUNK) write_frames(buf, CHUNK);
  (void)total;
}

// Equal temperament, A4 = 440.
#define C5 523.25f
#define D5 587.33f
#define E5 659.25f
#define G5 783.99f
#define A5 880.00f

void app_main(void) {
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
  ESP_LOGW(TAG, "if it buzzes instead of singing, suspect BCLK/LRC swapped "
                "(15 <-> 16); if it is thin and quiet, Vin is on 3V3 not 5V");

  // Let the clock settle before the amp comes up, so the first note is not a
  // pop. Zeros, not nothing: the amp wants a running clock.
  rest(120);
  amp_enabled(true);
  rest(80);

  for (;;) {
    ESP_LOGI(TAG, "1/5  hello — a rising third");
    chirp(E5, G5, 150, 0.9f);
    rest(400);

    ESP_LOGI(TAG, "2/5  happy — little arpeggio");
    chirp(C5, C5, 110, 0.8f);
    chirp(E5, E5, 110, 0.8f);
    chirp(G5, G5, 190, 0.9f);
    rest(500);

    ESP_LOGI(TAG, "3/5  curious — one note, bending up");
    chirp(G5, A5, 260, 0.85f);
    rest(400);

    ESP_LOGI(TAG, "4/5  settle — down and soft");
    chirp(G5, D5, 320, 0.6f);
    rest(600);

    // The reason this spike exists. Writing zeros is not silence: a class-D
    // amp keeps running and keeps hissing, and the buddy would hear itself.
    // The clock keeps running through the muted section on purpose, so a pass
    // means the amp went quiet — not that we stopped sending samples.
    ESP_LOGI(TAG, "5/5  mute test: note, SD low, note. Must be SILENT.");
    chirp(D5, D5, 400, 0.8f);
    amp_enabled(false);
    chirp(D5, D5, 400, 0.8f);                // still clocking, output stage off
    amp_enabled(true);
    chirp(D5, D5, 400, 0.8f);

    ESP_LOGI(TAG, "---- loop ----");
    rest(1500);
  }
}
