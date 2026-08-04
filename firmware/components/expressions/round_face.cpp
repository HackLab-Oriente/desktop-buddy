// GC9A01 240×240 round color face backend — the buddy's face. Renders the
// shared emotion model (face_model.h) in color, with a soft glow behind each
// eye and anti-aliased rounded corners.
//
// Panel driver: esp_lcd + espressif/esp_lcd_gc9a01 (managed component).
// Framebuffer lives in PSRAM (240*240*2 = 112 KB) and is pushed whole on each
// state change (blink, saccade, emotion, text) — not continuously.
#include "sdkconfig.h"

#include "bus.h"
#include "expressions.h"
#include "face_model.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "esp_lcd_gc9a01.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_log.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "face";

namespace buddy {
namespace {

constexpr int W = 240, H = 240;
constexpr float S = 2.6f;                 // model(128×64) → panel scale
constexpr int CX = 120, CY = 120, GAP = 42;  // eye centers at CX±GAP

// RGB565 (panel configured BGR + inverted; see panel_init()). Colors are
// EMO-inspired: a bright cyan eye on true black, with per-emotion mood tints.
constexpr uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
  return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}
constexpr uint16_t C_BG = rgb(0, 0, 0);

// Eye colors come from the shared model (same mood color as the LED ring).
// The base is the model color; the glow is a dim version of it.
inline uint16_t eye_base(int i) {
  return rgb(kEmotions[i].r, kEmotions[i].g, kEmotions[i].b);
}
inline uint16_t eye_glow(int i) {
  return rgb(kEmotions[i].r / 4, kEmotions[i].g / 4, kEmotions[i].b / 4);
}

esp_lcd_panel_handle_t s_panel = nullptr;
uint16_t* s_fb = nullptr;                 // 240×240 RGB565 in PSRAM
volatile int s_emotion = 0;
volatile bool s_dirty = true;

inline float clampf(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }

// Alpha-blend fg over bg in RGB565.
inline uint16_t blend(uint16_t bg, uint16_t fg, float a) {
  const int br = (bg >> 11) & 0x1F, bgg = (bg >> 5) & 0x3F, bb = bg & 0x1F;
  const int fr = (fg >> 11) & 0x1F, fgg = (fg >> 5) & 0x3F, fbb = fg & 0x1F;
  const int r = br + static_cast<int>((fr - br) * a + 0.5f);
  const int g = bgg + static_cast<int>((fgg - bgg) * a + 0.5f);
  const int b = bb + static_cast<int>((fbb - bb) * a + 0.5f);
  return static_cast<uint16_t>((r << 11) | (g << 5) | b);
}

void put(int x, int y, uint16_t c) {
  if (x >= 0 && x < W && y >= 0 && y < H) s_fb[y * W + x] = c;
}
void blend_at(int x, int y, uint16_t c, float a) {
  if (x < 0 || x >= W || y < 0 || y >= H || a <= 0.f) return;
  uint16_t& p = s_fb[y * W + x];
  p = blend(p, c, a > 1.f ? 1.f : a);
}
void clear() {
  for (int i = 0; i < W * H; i++) s_fb[i] = C_BG;
}

// Signed distance to a rounded rect centered at (cx,cy). Negative inside.
inline float sd_round_rect(float px, float py, float cx, float cy,
                           float hw, float hh, float r) {
  const float qx = fabsf(px - cx) - (hw - r);
  const float qy = fabsf(py - cy) - (hh - r);
  const float ax = qx > 0 ? qx : 0, ay = qy > 0 ? qy : 0;
  const float outside = sqrtf(ax * ax + ay * ay);
  const float inside = fminf(fmaxf(qx, qy), 0.f);
  return outside + inside - r;
}

// One anti-aliased eye: rounded lozenge with a soft glow halo, a slanted
// upper lid (brow) and a raised lower lid (happy squint) — all as smooth
// coverage. gaze_x/gaze_y shift where the eye looks.
void draw_eye(int cxi, int side, int open_pct, int gaze_x, int gaze_y) {
  const EyeStyle& e = kEmotions[s_emotion].eye;
  const uint16_t base_col = eye_base(s_emotion);
  const uint16_t glow_col = eye_glow(s_emotion);

  const float eyeW = e.width * S;
  float open = e.height * S * (e.openness / 100.f) * (open_pct / 100.f);
  if (open < 6.f) open = 6.f;
  const float cx = cxi + gaze_x, cy = CY + gaze_y;
  const float hw = eyeW / 2, hh = open / 2;
  float r = eyeW * 0.42f;
  if (r > hh) r = hh;

  const float browAmt = e.brow != 0 ? open * 0.5f : 0.f;
  const float top = cy - hh;
  const int gm = 10;  // glow margin
  const int x0 = static_cast<int>(cx - hw) - gm, x1 = static_cast<int>(cx + hw) + gm;
  const int y0 = static_cast<int>(top) - gm, y1 = static_cast<int>(cy + hh) + gm;

  for (int y = y0; y <= y1; y++) {
    for (int x = x0; x <= x1; x++) {
      const float d = sd_round_rect(x + 0.5f, y + 0.5f, cx, cy, hw, hh, r);
      float ecov = clampf(0.5f - d, 0.f, 1.f);
      const float gcov = clampf((7.f - d) / 7.f, 0.f, 1.f) * 0.5f;  // soft halo

      if (ecov > 0.f) {
        // brow: remove pixels above a slanted line near the top
        if (browAmt > 0.f) {
          const float t = clampf((x + 0.5f - (cx - hw)) / eyeW, 0.f, 1.f);
          const bool deep_right = (e.brow > 0) == (side == 0);
          const float frac = deep_right ? t : 1.f - t;
          const float cut_y = top + browAmt * frac;
          ecov *= clampf((y + 0.5f) - cut_y + 0.5f, 0.f, 1.f);
        }
        // happy squint: raise the lower lid
        if (e.lift > 0) {
          const float bot_y = cy + hh - e.lift * S;
          ecov *= clampf(bot_y - (y + 0.5f) + 0.5f, 0.f, 1.f);
        }
      }
      // Solid eye color (a vertical gradient bands badly in RGB565) + soft glow.
      if (gcov > 0.f) blend_at(x, y, glow_col, gcov);
      if (ecov > 0.f) blend_at(x, y, base_col, ecov);
    }
  }
}

void push() { esp_lcd_panel_draw_bitmap(s_panel, 0, 0, W, H, s_fb); }

void draw_eyes(int open_pct, int gaze_x, int gaze_y) {
  clear();
  draw_eye(CX - GAP, 0, open_pct, gaze_x, gaze_y);
  draw_eye(CX + GAP, 1, open_pct, gaze_x, gaze_y);
  push();
}

// 5x7 uppercase bitmap font — bigger and far more legible than the 3x5 model
// font. Order matches glyph_index(): A-Z, 0-9, space . , ! ? ' - :
// Bit 0b10000 = leftmost column.
constexpr uint8_t kFont57[][7] = {
    {0b01110,0b10001,0b10001,0b11111,0b10001,0b10001,0b10001},  // A
    {0b11110,0b10001,0b10001,0b11110,0b10001,0b10001,0b11110},  // B
    {0b01110,0b10001,0b10000,0b10000,0b10000,0b10001,0b01110},  // C
    {0b11110,0b10001,0b10001,0b10001,0b10001,0b10001,0b11110},  // D
    {0b11111,0b10000,0b10000,0b11110,0b10000,0b10000,0b11111},  // E
    {0b11111,0b10000,0b10000,0b11110,0b10000,0b10000,0b10000},  // F
    {0b01110,0b10001,0b10000,0b10111,0b10001,0b10001,0b01110},  // G
    {0b10001,0b10001,0b10001,0b11111,0b10001,0b10001,0b10001},  // H
    {0b11111,0b00100,0b00100,0b00100,0b00100,0b00100,0b11111},  // I
    {0b11111,0b00010,0b00010,0b00010,0b10010,0b10010,0b01100},  // J
    {0b10001,0b10010,0b10100,0b11000,0b10100,0b10010,0b10001},  // K
    {0b10000,0b10000,0b10000,0b10000,0b10000,0b10000,0b11111},  // L
    {0b10001,0b11011,0b10101,0b10101,0b10001,0b10001,0b10001},  // M
    {0b10001,0b11001,0b11001,0b10101,0b10011,0b10011,0b10001},  // N
    {0b01110,0b10001,0b10001,0b10001,0b10001,0b10001,0b01110},  // O
    {0b11110,0b10001,0b10001,0b11110,0b10000,0b10000,0b10000},  // P
    {0b01110,0b10001,0b10001,0b10001,0b10101,0b10010,0b01101},  // Q
    {0b11110,0b10001,0b10001,0b11110,0b10100,0b10010,0b10001},  // R
    {0b01111,0b10000,0b10000,0b01110,0b00001,0b00001,0b11110},  // S
    {0b11111,0b00100,0b00100,0b00100,0b00100,0b00100,0b00100},  // T
    {0b10001,0b10001,0b10001,0b10001,0b10001,0b10001,0b01110},  // U
    {0b10001,0b10001,0b10001,0b10001,0b10001,0b01010,0b00100},  // V
    {0b10001,0b10001,0b10001,0b10101,0b10101,0b11011,0b10001},  // W
    {0b10001,0b10001,0b01010,0b00100,0b01010,0b10001,0b10001},  // X
    {0b10001,0b10001,0b01010,0b00100,0b00100,0b00100,0b00100},  // Y
    {0b11111,0b00001,0b00010,0b00100,0b01000,0b10000,0b11111},  // Z
    {0b01110,0b10001,0b10011,0b10101,0b11001,0b10001,0b01110},  // 0
    {0b00100,0b01100,0b00100,0b00100,0b00100,0b00100,0b01110},  // 1
    {0b01110,0b10001,0b00001,0b00010,0b00100,0b01000,0b11111},  // 2
    {0b11111,0b00010,0b00100,0b00010,0b00001,0b10001,0b01110},  // 3
    {0b00010,0b00110,0b01010,0b10010,0b11111,0b00010,0b00010},  // 4
    {0b11111,0b10000,0b11110,0b00001,0b00001,0b10001,0b01110},  // 5
    {0b01110,0b10001,0b10000,0b11110,0b10001,0b10001,0b01110},  // 6
    {0b11111,0b00001,0b00010,0b00100,0b01000,0b01000,0b01000},  // 7
    {0b01110,0b10001,0b10001,0b01110,0b10001,0b10001,0b01110},  // 8
    {0b01110,0b10001,0b10001,0b01111,0b00001,0b10001,0b01110},  // 9
    {0b00000,0b00000,0b00000,0b00000,0b00000,0b00000,0b00000},  // space
    {0b00000,0b00000,0b00000,0b00000,0b00000,0b01100,0b01100},  // .
    {0b00000,0b00000,0b00000,0b00000,0b01100,0b01100,0b01000},  // ,
    {0b00100,0b00100,0b00100,0b00100,0b00100,0b00000,0b00100},  // !
    {0b01110,0b10001,0b00001,0b00110,0b00100,0b00000,0b00100},  // ?
    {0b00100,0b00100,0b01000,0b00000,0b00000,0b00000,0b00000},  // '
    {0b00000,0b00000,0b00000,0b11111,0b00000,0b00000,0b00000},  // -
    {0b00000,0b01100,0b01100,0b00000,0b01100,0b01100,0b00000},  // :
};

void draw_char(int x0, int y0, int sc, char ch, uint16_t c) {
  const uint8_t* g = kFont57[glyph_index(ch)];
  for (int r = 0; r < 7; r++)
    for (int col = 0; col < 5; col++)
      if (g[r] & (0b10000 >> col))
        for (int yy = 0; yy < sc; yy++)
          for (int xx = 0; xx < sc; xx++) put(x0 + col * sc + xx, y0 + r * sc + yy, c);
}

void draw_text(const char* text) {
  clear();
  constexpr int ML = 7, MC = 15;   // max lines, max chars/line (buffer is MC+1)
  char lines[ML][MC + 1] = {};
  int line = 0, col = 0;
  // Greedy word wrap with strict bounds — a long reply is truncated to ML
  // lines rather than overrunning the buffer (that was the reboot bug).
  const char* p = text;
  while (*p && line < ML) {
    while (*p == ' ') p++;                     // skip spaces
    const char* s = p;
    while (*p && *p != ' ') p++;               // scan one word
    int len = static_cast<int>(p - s);
    if (len == 0) break;
    if (len > MC) len = MC;                     // truncate an oversized word
    if (col > 0 && col + 1 + len > MC) {        // doesn't fit → new line
      if (++line >= ML) break;
      col = 0;
    }
    if (col > 0) lines[line][col++] = ' ';
    for (int i = 0; i < len; i++) lines[line][col++] = s[i];
  }
  const int used = (col > 0) ? line + 1 : line;
  if (used == 0) { push(); return; }

  const int sc = 2;             // 5x7 → 10x14 px glyphs (small, fits more)
  const int cell = sc * 6;      // glyph width + 1-col gap
  const int pitch = sc * 9;     // line height + gap
  int y = CY - (used * pitch) / 2;
  for (int l = 0; l < used; l++) {
    const int n = static_cast<int>(strlen(lines[l]));
    const int x = CX - (n * cell) / 2;
    for (int i = 0; i < n; i++) draw_char(x + i * cell, y, sc, lines[l][i], eye_base(0));
    y += pitch;
  }
  push();
}

std::mutex s_say_mu;
char s_say_text[128];
volatile int64_t s_say_until = 0;
volatile bool s_say_dirty = false;

// Gaze override — face.look sets a target; while active the eyes follow it
// instead of doing idle saccades. Any Sense or reflex can drive it: a tracked
// sprite, a detected face, a pack script. The target expires so the buddy
// always drifts back to its own idle behavior.
volatile int s_look_tx = 0, s_look_ty = 0;
volatile int64_t s_look_until = 0;

void face_task(void*) {
  int gaze_x = 0, gaze_y = 0;
  bool was_saying = false;
  int64_t next_blink = 2000, next_saccade = 1500;
  for (;;) {
    const int64_t now = esp_log_timestamp();
    if (now < s_say_until) {
      if (s_say_dirty) {
        std::lock_guard<std::mutex> lock(s_say_mu);
        s_say_dirty = false;
        draw_text(s_say_text);
      }
      was_saying = true;
      vTaskDelay(pdMS_TO_TICKS(40));
      continue;
    }
    if (was_saying) { was_saying = false; s_dirty = true; }

    const bool looking = now < s_look_until;
    if (looking) {  // ease toward the look target
      const int nx = gaze_x + (s_look_tx - gaze_x) / 2;
      const int ny = gaze_y + (s_look_ty - gaze_y) / 2;
      if (nx != gaze_x || ny != gaze_y || s_dirty) {
        gaze_x = nx; gaze_y = ny; s_dirty = false;
        draw_eyes(100, gaze_x, gaze_y);
      }
      if (now >= next_blink) {
        draw_eyes(12, gaze_x, gaze_y);
        vTaskDelay(pdMS_TO_TICKS(75));
        draw_eyes(100, gaze_x, gaze_y);
        const int period = kEmotions[s_emotion].blink_period_ms;
        next_blink = now + period / 2 + esp_random() % period;
      }
      vTaskDelay(pdMS_TO_TICKS(30));
      continue;
    }

    // idle: random saccades + blinks (gaze_y drifts back to 0)
    if (s_dirty) { s_dirty = false; draw_eyes(100, gaze_x, gaze_y); }
    if (now >= next_blink) {
      draw_eyes(12, gaze_x, gaze_y);
      vTaskDelay(pdMS_TO_TICKS(75));
      draw_eyes(100, gaze_x, gaze_y);
      const int period = kEmotions[s_emotion].blink_period_ms;
      next_blink = now + period / 2 + esp_random() % period;
    }
    if (now >= next_saccade) {
      gaze_x = static_cast<int>(esp_random() % 21) - 10;
      gaze_y = 0;
      draw_eyes(100, gaze_x, gaze_y);
      next_saccade = now + 800 + esp_random() % 2500;
    }
    vTaskDelay(pdMS_TO_TICKS(40));
  }
}

void panel_init() {
  spi_bus_config_t bus_cfg = {};
  bus_cfg.sclk_io_num = static_cast<gpio_num_t>(CONFIG_BUDDY_GC9A01_SCLK);
  bus_cfg.mosi_io_num = static_cast<gpio_num_t>(CONFIG_BUDDY_GC9A01_MOSI);
  bus_cfg.miso_io_num = GPIO_NUM_NC;
  bus_cfg.quadwp_io_num = GPIO_NUM_NC;
  bus_cfg.quadhd_io_num = GPIO_NUM_NC;
  bus_cfg.max_transfer_sz = W * H * 2 + 16;
  ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus_cfg, SPI_DMA_CH_AUTO));

  esp_lcd_panel_io_handle_t io = nullptr;
  esp_lcd_panel_io_spi_config_t io_cfg = {};
  io_cfg.cs_gpio_num = static_cast<gpio_num_t>(CONFIG_BUDDY_GC9A01_CS);
  io_cfg.dc_gpio_num = static_cast<gpio_num_t>(CONFIG_BUDDY_GC9A01_DC);
  io_cfg.spi_mode = 0;
  io_cfg.pclk_hz = 40 * 1000 * 1000;
  io_cfg.trans_queue_depth = 10;
  io_cfg.lcd_cmd_bits = 8;
  io_cfg.lcd_param_bits = 8;
  ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(
      static_cast<esp_lcd_spi_bus_handle_t>(SPI2_HOST), &io_cfg, &io));

  esp_lcd_panel_dev_config_t panel_cfg = {};
  panel_cfg.reset_gpio_num = static_cast<gpio_num_t>(CONFIG_BUDDY_GC9A01_RST);
  panel_cfg.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR;
  panel_cfg.bits_per_pixel = 16;
  ESP_ERROR_CHECK(esp_lcd_new_panel_gc9a01(io, &panel_cfg, &s_panel));

  ESP_ERROR_CHECK(esp_lcd_panel_reset(s_panel));
  ESP_ERROR_CHECK(esp_lcd_panel_init(s_panel));
  ESP_ERROR_CHECK(esp_lcd_panel_invert_color(s_panel, true));  // GC9A01 wants this
  // These modules come up horizontally mirrored (text reads backwards, brow
  // slants flip). Correct X. If yours ends up upside down or still mirrored,
  // adjust these two args — clone panels vary.
  ESP_ERROR_CHECK(esp_lcd_panel_mirror(s_panel, true, false));
  ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(s_panel, true));

#if CONFIG_BUDDY_GC9A01_BL >= 0
  gpio_set_direction(static_cast<gpio_num_t>(CONFIG_BUDDY_GC9A01_BL), GPIO_MODE_OUTPUT);
  gpio_set_level(static_cast<gpio_num_t>(CONFIG_BUDDY_GC9A01_BL), 1);
#endif
}

}  // namespace

void face_start() {
  panel_init();
  s_fb = static_cast<uint16_t*>(
      heap_caps_malloc(W * H * sizeof(uint16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA));
  if (!s_fb) s_fb = static_cast<uint16_t*>(heap_caps_malloc(W * H * sizeof(uint16_t), MALLOC_CAP_DMA));
  if (!s_fb) { ESP_LOGE(TAG, "no memory for framebuffer"); return; }

  bus().subscribe("face.emotion", [](const Event& ev) {
    int i = emotion_index(ev.payload.c_str());
    if (i >= 0) { s_emotion = i; s_dirty = true; }
    else ESP_LOGW(TAG, "unknown emotion '%s'", ev.payload.c_str());
  });
  bus().subscribe("face.say", [](const Event& ev) {
    std::lock_guard<std::mutex> lock(s_say_mu);
    strncpy(s_say_text, ev.payload.c_str(), sizeof s_say_text - 1);
    s_say_text[sizeof s_say_text - 1] = '\0';
    const int64_t hold = 1500 + 60 * static_cast<int64_t>(ev.payload.size());
    s_say_until = esp_log_timestamp() + (hold > 8000 ? 8000 : hold);
    s_say_dirty = true;
  });
  // face.look "x,y" (each -100..100) points the eyes at a spot on the screen.
  bus().subscribe("face.look", [](const Event& ev) {
    int x = 0, y = 0;
    sscanf(ev.payload.c_str(), "%d,%d", &x, &y);
    if (x < -100) x = -100; else if (x > 100) x = 100;
    if (y < -100) y = -100; else if (y > 100) y = 100;
    s_look_tx = x * 30 / 100;   // max ±30 px horizontal
    s_look_ty = y * 22 / 100;   // max ±22 px vertical
    s_look_until = esp_log_timestamp() + 1500;
  });

  draw_eyes(100, 0, 0);
  // Full-frame render + esp_lcd draw want more than the OLED path did.
  xTaskCreatePinnedToCore(face_task, "face", 6144, nullptr, 4, nullptr, 1);
}

}  // namespace buddy

