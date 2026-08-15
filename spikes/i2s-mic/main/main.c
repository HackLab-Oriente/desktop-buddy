// Spike: does the INMP441 actually hear anything, on the pins we planned?
//
// Isolated on purpose, same reasoning as the output spike: inside the real
// firmware a silent microphone could be the bus, a task priority, DMA, or the
// wiring. Here it can only be the wiring or the I2S config.
//
// This one does NOT touch the amplifier. "Record and play it back" is the
// session-1 goal, but it is two subsystems, and if you hear nothing you learn
// nothing about which half failed. Prove the microphone first; the pins are
// already compatible with the output spike (they share BCLK and WS), so
// joining them afterwards is small.
//
// What it does: reads the mic forever and prints a level meter plus peak, RMS
// and DC offset. Talk to it and the bar should move. The numbers are there
// because "the bar moved" and "the bar is pinned" and "the bar never moves"
// are three different bugs.
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "driver/i2s_std.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "i2s-mic";

// Proposed in hardware/buddy-s3-audio.md, and BCLK/WS are already shared with
// the MAX98357A in the output spike. Change here first, then there.
#define PIN_BCLK 15
#define PIN_WS   16
#define PIN_DIN  17     // INMP441 SD: the microphone talks, the ESP listens

// 16 kHz is what speech recognition wants, and it keeps BCLK at 1.024 MHz —
// exactly the 64x oversampling the INMP441 datasheet asks for.
#define RATE 16000

// The INMP441 is a 24-bit part that transmits inside a 32-bit slot, MSB first.
// Reading 32-bit slots and shifting down is the honest way round: ask for
// 24-bit slots and the frame no longer matches what the microphone sends.
#define SLOT_BITS I2S_DATA_BIT_WIDTH_32BIT

#define CHUNK 512               // frames per read: 32 ms at 16 kHz

static i2s_chan_handle_t s_rx;

// A 24-bit sample sits in the top bits of the 32-bit word, low 8 bits zero.
static inline int32_t to_24bit(int32_t raw) { return raw >> 8; }

static void meter(float rms, float peak) {
  // dBFS against the 24-bit full scale.
  const float full = 8388608.0f;
  const int db = (peak > 1.0f) ? (int)(20.0f * log10f(peak / full)) : -120;
  char bar[41];
  int n = (int)((db + 80) * 40 / 80);          // -80 dBFS .. 0 dBFS
  if (n < 0) n = 0;
  if (n > 40) n = 40;
  memset(bar, '#', n);
  memset(bar + n, '.', 40 - n);
  bar[40] = '\0';
  ESP_LOGI(TAG, "[%s] %4d dBFS  peak=%8ld  rms=%8.0f", bar, db, (long)peak, rms);
}

void app_main(void) {
  i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
  ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, NULL, &s_rx));

  i2s_std_config_t std_cfg = {
      .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(RATE),
      .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(SLOT_BITS, I2S_SLOT_MODE_MONO),
      .gpio_cfg = {
          .mclk = I2S_GPIO_UNUSED,
          .bclk = PIN_BCLK,
          .ws = PIN_WS,
          .dout = I2S_GPIO_UNUSED,
          .din = PIN_DIN,
      },
  };
  // L/R tied to GND makes the INMP441 talk in the LEFT slot. Listening to the
  // right slot is the classic way to get a perfectly working microphone that
  // reads pure silence.
  std_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT;
  ESP_ERROR_CHECK(i2s_channel_init_std_mode(s_rx, &std_cfg));
  ESP_ERROR_CHECK(i2s_channel_enable(s_rx));

  ESP_LOGI(TAG, "i2s in: bclk=%d ws=%d din=%d, %d Hz, %d-bit slots, LEFT slot",
           PIN_BCLK, PIN_WS, PIN_DIN, RATE, (int)SLOT_BITS);
  ESP_LOGW(TAG, "reading what the microphone says. Talk to it.");

  int32_t *buf = malloc(CHUNK * sizeof(int32_t));
  if (!buf) { ESP_LOGE(TAG, "no memory"); return; }

  bool dumped = false;
  int64_t frames = 0;

  for (;;) {
    size_t got = 0;
    const esp_err_t err =
        i2s_channel_read(s_rx, buf, CHUNK * sizeof(int32_t), &got, 1000);
    if (err != ESP_OK) { ESP_LOGE(TAG, "read: %s", esp_err_to_name(err)); continue; }
    const int n = got / sizeof(int32_t);
    if (n == 0) continue;

    // First chunk raw, before any interpretation. All 00000000 means no data
    // line; all FFFFFFFF means it is stuck high. Either way the arithmetic
    // below would just report a very confident silence.
    if (!dumped) {
      dumped = true;
      ESP_LOGW(TAG, "first 8 raw words: %08lx %08lx %08lx %08lx %08lx %08lx %08lx %08lx",
               (unsigned long)buf[0], (unsigned long)buf[1], (unsigned long)buf[2],
               (unsigned long)buf[3], (unsigned long)buf[4], (unsigned long)buf[5],
               (unsigned long)buf[6], (unsigned long)buf[7]);
    }

    double sum = 0, sumsq = 0;
    float peak = 0;
    int zeros = 0;
    for (int i = 0; i < n; i++) {
      const int32_t s = to_24bit(buf[i]);
      if (s == 0) zeros++;
      const float a = fabsf((float)s);
      if (a > peak) peak = a;
      sum += s;
      sumsq += (double)s * s;
    }
    const float mean = (float)(sum / n);
    const float rms = sqrtf((float)(sumsq / n));
    frames += n;

    // Once a second, not once per chunk: 31 lines a second is unreadable.
    if (frames >= RATE) {
      frames = 0;
      meter(rms, peak);
      // DC offset matters. A MEMS microphone sits near zero; a large constant
      // offset with no variation means the bits are not audio at all.
      if (zeros == n) {
        ESP_LOGE(TAG, "every sample is exactly zero — check SD on GPIO %d, and "
                      "that L/R is tied to GND (not floating)", PIN_DIN);
      } else if (peak < 2000) {
        ESP_LOGW(TAG, "signal present but very quiet (peak=%ld, dc=%.0f). If "
                      "shouting does not move it, suspect L/R or the slot mask",
                 (long)peak, mean);
      }
    }
  }
}
