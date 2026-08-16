// Spike: the push-to-talk loop. Hold a button, talk, let go, hear yourself.
//
// This is the session-1 bar for the voice track, and it proves the mechanism
// the whole PTT design rests on: the speaker is HARD-MUTED while the
// microphone records. Not "volume down" — the amplifier's output stage off.
// Without that the buddy hears itself and the loop is worthless.
//
// Isolated on purpose: inside the real firmware a silent recording could be
// the bus, a task priority, DMA or the wiring. Here it can only be the wiring
// or the I2S config.
//
// ---------------------------------------------------------------------------
// The design decision worth knowing: the I2S port is torn down and rebuilt on
// every direction change, instead of running one full-duplex channel pair.
//
// It costs a few milliseconds you cannot perceive (it happens when you release
// the button), and it buys two things. Each direction uses byte-for-byte the
// configuration already proven by its own spike — mono/LEFT 32-bit slots in,
// stereo 32-bit slots out — so a failure here cannot be "the shared framing
// was subtly wrong". And it makes half-duplex true in hardware rather than by
// convention: the peripheral is physically incapable of doing both at once.
// ---------------------------------------------------------------------------
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/i2s_std.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "i2s-mic";

// Proposed in hardware/buddy-s3-audio.md. BCLK and WS are shared with the
// amplifier on purpose — half-duplex means they can be.
#define PIN_BCLK 15
#define PIN_WS   16
#define PIN_DIN  17     // INMP441 SD: the microphone talks, the ESP listens
#define PIN_DOUT 18     // MAX98357A DIN: the buddy talks
#define PIN_MUTE 2      // MAX98357A SD: low = output stage off, high = on
#define PIN_PTT  5      // button to GND, internal pull-up: low = held

#define RATE 16000
#define SLOT_BITS I2S_DATA_BIT_WIDTH_32BIT
#define CHUNK 512                       // frames per read: 32 ms at 16 kHz
// 30 s is 960 KB in PSRAM, of which the S3 has 8 MB — this ceiling exists to
// catch a stuck button, not because memory is short. At 32 KB/s the chip could
// hold four minutes. The real limit on a voice loop is the STT upload, not RAM.
#define MAX_SECONDS 30
#define MAX_FRAMES (RATE * MAX_SECONDS)
#define FULL_SCALE 8388608.0f           // 24-bit

static i2s_chan_handle_t s_rx, s_tx;
static float s_peak;                    // of the last take, 24-bit scale
static int16_t *s_rec;                  // PSRAM; 16-bit is what STT wants anyway
static int32_t *s_chunk;

static inline int32_t to_24bit(int32_t raw) { return raw >> 8; }
static float dbfs(float v) { return v > 1.0f ? 20.0f * log10f(v / FULL_SCALE) : -120.0f; }

static void amp_enabled(bool on) { gpio_set_level(PIN_MUTE, on ? 1 : 0); }
static bool ptt_held(void) { return gpio_get_level(PIN_PTT) == 0; }

// --- I2S, one direction at a time -----------------------------------------
static const i2s_std_gpio_config_t k_gpio_in = {
    .mclk = I2S_GPIO_UNUSED, .bclk = PIN_BCLK, .ws = PIN_WS,
    .dout = I2S_GPIO_UNUSED, .din = PIN_DIN,
};
static const i2s_std_gpio_config_t k_gpio_out = {
    .mclk = I2S_GPIO_UNUSED, .bclk = PIN_BCLK, .ws = PIN_WS,
    .dout = PIN_DOUT, .din = I2S_GPIO_UNUSED,
};

static void i2s_open_rx(void) {
  i2s_chan_config_t cc = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
  ESP_ERROR_CHECK(i2s_new_channel(&cc, NULL, &s_rx));
  i2s_std_config_t cfg = {
      .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(RATE),
      .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(SLOT_BITS, I2S_SLOT_MODE_MONO),
      .gpio_cfg = k_gpio_in,
  };
  // L/R tied to GND makes the INMP441 talk in the LEFT slot. Listening to the
  // right one is the classic way to get a working microphone that reads silence.
  cfg.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT;
  ESP_ERROR_CHECK(i2s_channel_init_std_mode(s_rx, &cfg));
  ESP_ERROR_CHECK(i2s_channel_enable(s_rx));
}

static void i2s_open_tx(void) {
  i2s_chan_config_t cc = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
  ESP_ERROR_CHECK(i2s_new_channel(&cc, &s_tx, NULL));
  i2s_std_config_t cfg = {
      .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(RATE),
      .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(SLOT_BITS, I2S_SLOT_MODE_STEREO),
      .gpio_cfg = k_gpio_out,
  };
  ESP_ERROR_CHECK(i2s_channel_init_std_mode(s_tx, &cfg));
  ESP_ERROR_CHECK(i2s_channel_enable(s_tx));
}

static void i2s_close(i2s_chan_handle_t *h) {
  if (!*h) return;
  i2s_channel_disable(*h);
  i2s_del_channel(*h);
  *h = NULL;
}

// --- the loop --------------------------------------------------------------
static void meter(float peak, float rms) {
  const int db = (int)dbfs(peak);
  char bar[41];
  int n = (db + 80) * 40 / 80;
  if (n < 0) n = 0;
  if (n > 40) n = 40;
  memset(bar, '#', n);
  memset(bar + n, '.', 40 - n);
  bar[40] = '\0';
  ESP_LOGI(TAG, "[%s] %4d dBFS  peak=%8ld  rms=%8.0f  (hold GPIO%d to record)",
           bar, db, (long)peak, rms, PIN_PTT);
}

// Record until the button comes back up. Returns frames captured.
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
      const float a = fabsf((float)s);
      if (a > peak) peak = a;
      if (a >= FULL_SCALE - 2) clipped++;
      sumsq += (double)s * s;
      s_rec[frames++] = (int16_t)(s >> 8);   // 24-bit -> 16-bit
    }
  }

  s_peak = peak;
  const float rms = frames ? sqrtf((float)(sumsq / frames)) : 0;
  ESP_LOGI(TAG, "captured %.2f s — peak %.1f dBFS, rms %.1f dBFS, %d clipped",
           (double)frames / RATE, (double)dbfs(peak), (double)dbfs(rms), clipped);
  if (frames >= MAX_FRAMES)
    ESP_LOGW(TAG, "hit the %d s ceiling — released late, or the button is stuck low",
             MAX_SECONDS);
  if (peak > FULL_SCALE * 0.9f)
    ESP_LOGW(TAG, "clipping: back off or the recording will sound broken");
  else if (dbfs(peak) < -45.0f)
    ESP_LOGW(TAG, "very quiet: closer to the mic, or the loop needs digital gain");
  return frames;
}

static void playback(size_t frames) {
  // Make-up gain, and this is the big volume lever. A voice recorded at, say,
  // -30 dBFS played back untouched drives the amplifier to 3% of what it can
  // do — so the speaker is quiet because the RECORDING is quiet, not because
  // the amplifier is weak. Normalising to about -3 dBFS is worth ~27 dB here;
  // the MAX98357A's GAIN pin, for comparison, can offer at most 6 dB.
  const float peak16 = s_peak / 256.0f;            // stored samples are 16-bit
  float gain = (peak16 > 8.0f) ? 23000.0f / peak16 : 1.0f;
  if (gain < 1.0f) gain = 1.0f;                    // never make it quieter
  if (gain > 16.0f) gain = 16.0f;                  // +24 dB: past this you are
                                                   // just amplifying the hiss
  ESP_LOGI(TAG, "playing back %.2f s with %+.1f dB make-up gain",
           (double)frames / RATE, (double)(20.0f * log10f(gain)));
  amp_enabled(true);
  vTaskDelay(pdMS_TO_TICKS(20));            // let the amp's output stage settle

  // Mono duplicated into both slots: the amp's SD pin is floating-ish here and
  // averages L+R, so sending one slot only would cost 6 dB for nothing.
  for (size_t i = 0; i < frames; i += CHUNK / 2) {
    const size_t n = (frames - i < CHUNK / 2) ? frames - i : CHUNK / 2;
    for (size_t j = 0; j < n; j++) {
      int32_t v = (int32_t)(s_rec[i + j] * gain);
      if (v > 32767) v = 32767; else if (v < -32768) v = -32768;
      const int32_t s = v << 16;                       // 16-bit -> 32-bit slot
      s_chunk[j * 2] = s;
      s_chunk[j * 2 + 1] = s;
    }
    size_t wrote = 0;
    i2s_channel_write(s_tx, s_chunk, n * 2 * sizeof(int32_t), &wrote, 1000);
  }
  vTaskDelay(pdMS_TO_TICKS(40));            // drain before cutting the clock
  amp_enabled(false);
}

void app_main(void) {
  gpio_config_t mute = {.pin_bit_mask = 1ULL << PIN_MUTE, .mode = GPIO_MODE_OUTPUT};
  ESP_ERROR_CHECK(gpio_config(&mute));
  amp_enabled(false);                       // silent until we mean it

  gpio_config_t btn = {.pin_bit_mask = 1ULL << PIN_PTT,
                       .mode = GPIO_MODE_INPUT,
                       .pull_up_en = GPIO_PULLUP_ENABLE};
  ESP_ERROR_CHECK(gpio_config(&btn));

  s_rec = heap_caps_malloc(MAX_FRAMES * sizeof(int16_t), MALLOC_CAP_SPIRAM);
  s_chunk = heap_caps_malloc(CHUNK * sizeof(int32_t), MALLOC_CAP_DMA);
  if (!s_rec || !s_chunk) {
    ESP_LOGE(TAG, "no memory — is PSRAM enabled? (%d KB wanted)",
             (int)(MAX_FRAMES * sizeof(int16_t) / 1024));
    return;
  }

  i2s_open_rx();
  ESP_LOGI(TAG, "bclk=%d ws=%d din=%d dout=%d mute=%d ptt=%d, %d Hz",
           PIN_BCLK, PIN_WS, PIN_DIN, PIN_DOUT, PIN_MUTE, PIN_PTT, RATE);
  ESP_LOGW(TAG, "idle = level meter. Hold GPIO%d to record, release to hear it.",
           PIN_PTT);

  int64_t frames_seen = 0;
  float peak = 0;
  double sumsq = 0;

  for (;;) {
    if (ptt_held()) {
      vTaskDelay(pdMS_TO_TICKS(30));        // debounce the press
      if (!ptt_held()) continue;

      const size_t frames = record();
      i2s_close(&s_rx);                     // the port can only face one way
      if (frames > RATE / 10) {             // ignore an accidental tap
        i2s_open_tx();
        playback(frames);
        i2s_close(&s_tx);
      } else {
        ESP_LOGW(TAG, "too short to play back — hold the button while talking");
      }
      i2s_open_rx();
      while (ptt_held()) vTaskDelay(pdMS_TO_TICKS(20));   // don't retrigger
      peak = 0; sumsq = 0; frames_seen = 0;
      continue;
    }

    size_t got = 0;
    if (i2s_channel_read(s_rx, s_chunk, CHUNK * sizeof(int32_t), &got, 100) != ESP_OK)
      continue;
    const int n = got / sizeof(int32_t);
    for (int i = 0; i < n; i++) {
      const int32_t s = to_24bit(s_chunk[i]);
      const float a = fabsf((float)s);
      if (a > peak) peak = a;
      sumsq += (double)s * s;
    }
    frames_seen += n;
    if (frames_seen >= RATE) {
      meter(peak, sqrtf((float)(sumsq / frames_seen)));
      frames_seen = 0; peak = 0; sumsq = 0;
    }
  }
}
