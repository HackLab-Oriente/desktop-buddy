// Buddy Zero's face: two parametric eyes on an SSD1306 128×64 OLED.
// PoC of the *expression model* (emotions → eye parameters, blink timing,
// saccades, brow slants) plus a retro text mode so the buddy's words show
// on screen ("face.say"). The model ports to LVGL/GC9A01 for v1.
#include "bus.h"
#include "expressions.h"

#include <cctype>
#include <cstring>
#include <mutex>
#include <string>

#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "face";

namespace buddy {
namespace {

constexpr int W = 128, H = 64;
constexpr uint8_t ADDR = 0x3C;
uint8_t fb[W * H / 8];
i2c_master_dev_handle_t s_dev;

void cmd(uint8_t c) {
  uint8_t buf[2] = {0x00, c};
  i2c_master_transmit(s_dev, buf, 2, 50);
}

void flush() {
  cmd(0x21); cmd(0); cmd(W - 1);   // column range
  cmd(0x22); cmd(0); cmd(7);       // page range
  static uint8_t out[1 + sizeof fb];
  out[0] = 0x40;
  memcpy(out + 1, fb, sizeof fb);
  i2c_master_transmit(s_dev, out, sizeof out, 100);
}

void px_on(int x, int y) {
  if (x >= 0 && x < W && y >= 0 && y < H) fb[x + (y / 8) * W] |= 1 << (y % 8);
}
void px_off(int x, int y) {
  if (x >= 0 && x < W && y >= 0 && y < H) fb[x + (y / 8) * W] &= ~(1 << (y % 8));
}
void fill_rect(int x, int y, int w, int h) {
  for (int j = y; j < y + h; j++)
    for (int i = x; i < x + w; i++) px_on(i, j);
}

// --- Emotions ------------------------------------------------------------
// brow: +1 = angry (lids slant down toward the nose), -1 = sad (toward the
// temples), 0 = none. lift raises the lower lid ("happy squint").
struct EyeStyle {
  int width, height, openness, lift, brow;
};
struct Emotion {
  const char* name;
  EyeStyle eye;
  int blink_period_ms;
};
constexpr Emotion kEmotions[] = {
    {"neutral",   {26, 30, 100, 0,  0},  3800},
    {"happy",     {26, 30, 100, 14, 0},  3000},
    {"curious",   {30, 34, 100, 0,  0},  2600},
    {"sleepy",    {26, 30, 35,  0,  0},  6000},
    {"surprised", {34, 40, 100, 0,  0},  5000},
    {"angry",     {28, 28, 100, 0,  1},  3200},
    {"sad",       {24, 26, 75,  0, -1},  5200},
    {"suspicious", {26, 30, 55, 0, 1}, 2200},
};

volatile int s_emotion = 0;

void draw_eyes(int openness_pct, int gaze_dx) {
  memset(fb, 0, sizeof fb);
  const EyeStyle& e = kEmotions[s_emotion].eye;
  const int open = e.height * e.openness / 100 * openness_pct / 100;
  const int cy = 32, lx = 34 + gaze_dx, rx = 94 + gaze_dx;
  const int brow_depth = open / 3;
  for (int side = 0; side < 2; side++) {
    const int cx = side == 0 ? lx : rx;
    const int x0 = cx - e.width / 2, top = cy - open / 2;
    fill_rect(x0, top, e.width, open < 2 ? 2 : open);
    if (e.lift > 0)  // carve the lower lid: happy squint
      for (int j = cy + open / 2 - e.lift; j < cy + open / 2; j++)
        for (int i = x0; i < x0 + e.width; i++) px_off(i, j);
    if (e.brow != 0) {
      // Carve a sloped upper lid. Angry slopes deeper toward the nose
      // (inner edge), sad toward the temples (outer edge).
      for (int i = 0; i < e.width; i++) {
        const bool inner_deep = (e.brow > 0) == (side == 0);
        const int t = inner_deep ? i : e.width - 1 - i;
        const int depth = brow_depth * t / e.width;
        for (int j = 0; j < depth; j++) px_off(x0 + i, top + j);
      }
    }
  }
  flush();
}

// --- Text mode (3×5 uppercase font, rendered 2×) -------------------------
// 16 chars × 4 lines. Retro-cute and hand-verifiable, not typographically
// complete: unknown characters render as space.
constexpr uint8_t kFont[][5] = {
    {0b010,0b101,0b111,0b101,0b101}, {0b110,0b101,0b110,0b101,0b110},  // A B
    {0b011,0b100,0b100,0b100,0b011}, {0b110,0b101,0b101,0b101,0b110},  // C D
    {0b111,0b100,0b110,0b100,0b111}, {0b111,0b100,0b110,0b100,0b100},  // E F
    {0b011,0b100,0b101,0b101,0b011}, {0b101,0b101,0b111,0b101,0b101},  // G H
    {0b111,0b010,0b010,0b010,0b111}, {0b001,0b001,0b001,0b101,0b010},  // I J
    {0b101,0b110,0b100,0b110,0b101}, {0b100,0b100,0b100,0b100,0b111},  // K L
    {0b101,0b111,0b111,0b101,0b101}, {0b110,0b101,0b101,0b101,0b101},  // M N
    {0b010,0b101,0b101,0b101,0b010}, {0b110,0b101,0b110,0b100,0b100},  // O P
    {0b010,0b101,0b101,0b010,0b001}, {0b110,0b101,0b110,0b110,0b101},  // Q R
    {0b011,0b100,0b010,0b001,0b110}, {0b111,0b010,0b010,0b010,0b010},  // S T
    {0b101,0b101,0b101,0b101,0b111}, {0b101,0b101,0b101,0b101,0b010},  // U V
    {0b101,0b101,0b111,0b111,0b101}, {0b101,0b101,0b010,0b101,0b101},  // W X
    {0b101,0b101,0b010,0b010,0b010}, {0b111,0b001,0b010,0b100,0b111},  // Y Z
    {0b111,0b101,0b101,0b101,0b111}, {0b010,0b110,0b010,0b010,0b111},  // 0 1
    {0b110,0b001,0b010,0b100,0b111}, {0b110,0b001,0b010,0b001,0b110},  // 2 3
    {0b101,0b101,0b111,0b001,0b001}, {0b111,0b100,0b110,0b001,0b110},  // 4 5
    {0b011,0b100,0b110,0b101,0b010}, {0b111,0b001,0b010,0b010,0b010},  // 6 7
    {0b111,0b101,0b111,0b101,0b111}, {0b010,0b101,0b011,0b001,0b110},  // 8 9
    {0b000,0b000,0b000,0b000,0b000}, {0b000,0b000,0b000,0b000,0b010},  // sp .
    {0b000,0b000,0b000,0b010,0b100}, {0b010,0b010,0b010,0b000,0b010},  // , !
    {0b110,0b001,0b010,0b000,0b010}, {0b010,0b010,0b000,0b000,0b000},  // ? '
    {0b000,0b000,0b111,0b000,0b000}, {0b000,0b010,0b000,0b010,0b000},  // - :
};

int glyph_index(char c) {
  c = toupper(static_cast<unsigned char>(c));
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= '0' && c <= '9') return 26 + (c - '0');
  switch (c) {
    case '.': return 37; case ',': return 38; case '!': return 39;
    case '?': return 40; case '\'': return 41; case '-': return 42;
    case ':': return 43; default: return 36;  // space
  }
}

void draw_text(const char* text) {
  memset(fb, 0, sizeof fb);
  int col = 0, line = 0;
  for (const char* p = text; *p && line < 4; p++) {
    if (*p == ' ' && col == 0) continue;  // no leading spaces after a wrap
    const uint8_t* g = kFont[glyph_index(*p)];
    const int x0 = col * 8 + 1, y0 = line * 14 + 5;
    for (int r = 0; r < 5; r++)
      for (int c = 0; c < 3; c++)
        if (g[r] & (0b100 >> c)) fill_rect(x0 + c * 2, y0 + r * 2, 2, 2);
    if (++col >= 16) { col = 0; line++; }
  }
  flush();
}

std::mutex s_say_mu;
char s_say_text[128];
volatile int64_t s_say_until = 0;
volatile bool s_say_dirty = false;

void face_task(void*) {
  int gaze = 0;
  bool was_saying = false;
  int64_t next_blink = 2000, next_saccade = 1500;
  for (;;) {
    const int64_t now = esp_log_timestamp();

    if (now < s_say_until) {  // text mode: words own the screen
      if (s_say_dirty) {
        std::lock_guard<std::mutex> lock(s_say_mu);
        s_say_dirty = false;
        draw_text(s_say_text);
      }
      was_saying = true;
      vTaskDelay(pdMS_TO_TICKS(40));
      continue;
    }
    if (was_saying) {  // text finished: bring the eyes back
      was_saying = false;
      draw_eyes(100, gaze);
    }

    if (now >= next_blink) {
      draw_eyes(15, gaze);
      vTaskDelay(pdMS_TO_TICKS(70));
      draw_eyes(100, gaze);
      const int period = kEmotions[s_emotion].blink_period_ms;
      next_blink = now + period / 2 + esp_random() % period;
    }
    if (now >= next_saccade) {
      gaze = static_cast<int>(esp_random() % 13) - 6;
      draw_eyes(100, gaze);
      next_saccade = now + 800 + esp_random() % 2500;
    }
    vTaskDelay(pdMS_TO_TICKS(40));
  }
}

}  // namespace

void oled_face_start(int gpio_sda, int gpio_scl) {
  i2c_master_bus_config_t bus_cfg = {};
  bus_cfg.i2c_port = -1;  // auto-select
  bus_cfg.sda_io_num = static_cast<gpio_num_t>(gpio_sda);
  bus_cfg.scl_io_num = static_cast<gpio_num_t>(gpio_scl);
  bus_cfg.clk_source = I2C_CLK_SRC_DEFAULT;
  bus_cfg.glitch_ignore_cnt = 7;
  bus_cfg.flags.enable_internal_pullup = true;
  i2c_master_bus_handle_t i2c_bus;
  ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &i2c_bus));

  i2c_device_config_t dev_cfg = {};
  dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
  dev_cfg.device_address = ADDR;
  dev_cfg.scl_speed_hz = 400000;
  ESP_ERROR_CHECK(i2c_master_bus_add_device(i2c_bus, &dev_cfg, &s_dev));

  // SSD1306 init (charge pump on, alternating COM pins, 128×64)
  for (uint8_t c : {0xAEu, 0xD5u, 0x80u, 0xA8u, 0x3Fu, 0xD3u, 0x00u, 0x40u,
                    0x8Du, 0x14u, 0x20u, 0x00u, 0xA1u, 0xC8u, 0xDAu, 0x12u,
                    0x81u, 0xCFu, 0xD9u, 0xF1u, 0xDBu, 0x40u, 0xA4u, 0xA6u, 0xAFu})
    cmd(static_cast<uint8_t>(c));

  bus().subscribe("face.emotion", [](const Event& ev) {
    for (size_t i = 0; i < sizeof kEmotions / sizeof kEmotions[0]; i++) {
      if (ev.payload == kEmotions[i].name) {
        s_emotion = static_cast<int>(i);
        return;
      }
    }
    ESP_LOGW(TAG, "unknown emotion '%s'", ev.payload.c_str());
  });

  bus().subscribe("face.say", [](const Event& ev) {
    std::lock_guard<std::mutex> lock(s_say_mu);
    strncpy(s_say_text, ev.payload.c_str(), sizeof s_say_text - 1);
    s_say_text[sizeof s_say_text - 1] = '\0';
    const int64_t hold = 1500 + 60 * static_cast<int64_t>(ev.payload.size());
    s_say_until = esp_log_timestamp() + (hold > 8000 ? 8000 : hold);
    s_say_dirty = true;
  });

  draw_eyes(100, 0);
  xTaskCreatePinnedToCore(face_task, "face", 4096, nullptr, 4, nullptr, 1);
}

}  // namespace buddy
