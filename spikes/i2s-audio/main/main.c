// Spike: does sound come out of the MAX98357A on the pins we planned?
//
// Isolated on purpose — this is the whole app. When I2S goes into the real
// firmware, a failure is ambiguous (bus? task priority? PSRAM?); here it can
// only be the wiring or the I2S config.
//
// Runs four steps in a loop, each announced on the serial log so you can match
// what you hear to what it is doing:
//   1. 440 Hz sine        — is there sound at all, and is it clean?
//   2. silence            — is the amp quiet when fed zeros?
//   3. sweep 200-2000 Hz  — is the whole band there, or is it buzzing?
//   4. HARDWARE MUTE      — the one that matters for the voice loop
//
// Step 4 is the point of this spike. Feeding zeros is not silence: a class-D
// amp still runs and still hisses, and the buddy would hear itself. Pulling
// the SD pin low shuts the output stage off. That is the half-duplex mechanism
// the push-to-talk loop depends on, so it gets proven before anything else is
// built on top of it.
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
#define CHUNK       256                     // frames per write
#define AMPLITUDE   8000                    // of 32767 — deliberately not loud

static i2s_chan_handle_t s_tx;

static void amp_enabled(bool on) {
  gpio_set_level(PIN_MUTE, on ? 1 : 0);
}

// Write `ms` of a tone. freq_end != freq_start sweeps between them.
// phase is carried across calls so consecutive chunks do not click.
static void play(float freq_start, float freq_end, int ms) {
  static float phase = 0.0f;
  const int total = SAMPLE_RATE * ms / 1000;
  int16_t buf[CHUNK * 2];                   // stereo: the amp reads one slot,
                                            // but both get the same sample so
                                            // the SD-pin channel select cannot
                                            // pick the silent one by accident
  for (int done = 0; done < total; done += CHUNK) {
    const int n = (total - done < CHUNK) ? (total - done) : CHUNK;
    for (int i = 0; i < n; i++) {
      const float t = (float)(done + i) / (float)total;
      const float freq = freq_start + (freq_end - freq_start) * t;
      phase += 2.0f * (float)M_PI * freq / SAMPLE_RATE;
      if (phase > 2.0f * (float)M_PI) phase -= 2.0f * (float)M_PI;
      const int16_t s = (int16_t)(sinf(phase) * AMPLITUDE);
      buf[i * 2] = s;
      buf[i * 2 + 1] = s;
    }
    size_t written = 0;
    i2s_channel_write(s_tx, buf, (size_t)n * 2 * sizeof(int16_t), &written, 1000);
  }
}

static void silence(int ms) {
  int16_t buf[CHUNK * 2];
  memset(buf, 0, sizeof buf);
  const int total = SAMPLE_RATE * ms / 1000;
  for (int done = 0; done < total; done += CHUNK) {
    size_t written = 0;
    i2s_channel_write(s_tx, buf, sizeof buf, &written, 1000);
  }
}

void app_main(void) {
  gpio_config_t mute = {
      .pin_bit_mask = 1ULL << PIN_MUTE,
      .mode = GPIO_MODE_OUTPUT,
  };
  ESP_ERROR_CHECK(gpio_config(&mute));
  amp_enabled(false);                       // stay quiet until the clock is up

  i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
  ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &s_tx, NULL));

  i2s_std_config_t std_cfg = {
      .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
      .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                      I2S_SLOT_MODE_STEREO),
      .gpio_cfg = {
          .mclk = I2S_GPIO_UNUSED,          // the MAX98357A makes its own
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
  ESP_LOGW(TAG, "if you hear nothing at all, check Vin (5V) and that the two "
                "speaker wires go ONLY to + and -, never to GND");

  for (;;) {
    amp_enabled(true);

    ESP_LOGI(TAG, "1/4  440 Hz tone");
    play(440.0f, 440.0f, 1000);

    ESP_LOGI(TAG, "2/4  zeros (amp still on — a faint hiss here is normal)");
    silence(700);

    ESP_LOGI(TAG, "3/4  sweep 200 -> 2000 Hz");
    play(200.0f, 2000.0f, 1200);

    ESP_LOGI(TAG, "4/4  mute test: tone, SD low for 400 ms, tone");
    play(660.0f, 660.0f, 400);
    amp_enabled(false);
    ESP_LOGI(TAG, "     ...SD low. This has to be SILENT, not quieter.");
    play(660.0f, 660.0f, 400);               // still clocking, output stage off
    amp_enabled(true);
    play(660.0f, 660.0f, 400);

    ESP_LOGI(TAG, "---- loop ----");
    silence(1000);
  }
}
