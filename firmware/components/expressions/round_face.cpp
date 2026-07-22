// GC9A01 240×240 round color face backend — the v1 display. Renders the same
// shared emotion model (face_model.h) as the OLED, but bigger, in color, with
// a soft glow behind each eye and anti-aliased rounded corners. Compiled only
// when this backend is selected in menuconfig.
//
// Panel driver: esp_lcd + espressif/esp_lcd_gc9a01 (managed component).
// Framebuffer lives in PSRAM (240*240*2 = 112 KB) and is pushed whole on each
// state change (blink, saccade, emotion, text) — not continuously.
#include "sdkconfig.h"
#if CONFIG_BUDDY_DISPLAY_GC9A01

#include "bus.h"
#include "expressions.h"
#include "face_model.h"

#include <cmath>
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

// RGB565 helpers (panel configured BGR + inverted; see face_start()).
constexpr uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
  return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}
constexpr uint16_t C_BG   = rgb(4, 6, 10);
constexpr uint16_t C_EYE  = rgb(60, 235, 180);   // mint
constexpr uint16_t C_GLOW = rgb(14, 60, 48);     // dim halo

esp_lcd_panel_handle_t s_panel = nullptr;
uint16_t* s_fb = nullptr;                 // 240×240 RGB565 in PSRAM
volatile int s_emotion = 0;
volatile bool s_dirty = true;

void put(int x, int y, uint16_t c) {
  if (x >= 0 && x < W && y >= 0 && y < H) s_fb[y * W + x] = c;
}

void clear() {
  for (int i = 0; i < W * H; i++) s_fb[i] = C_BG;
}

// Filled rounded rect. paint==C_BG is used to carve (lids, brows).
void round_rect(int x0, int y0, int w, int h, int r, uint16_t c) {
  if (w <= 0 || h <= 0) return;
  if (r > w / 2) r = w / 2;
  if (r > h / 2) r = h / 2;
  const int x1 = x0 + w - 1, y1 = y0 + h - 1;
  for (int y = y0; y <= y1; y++) {
    for (int x = x0; x <= x1; x++) {
      int dx = 0, dy = 0;
      if (x < x0 + r) dx = x0 + r - x; else if (x > x1 - r) dx = x - (x1 - r);
      if (y < y0 + r) dy = y0 + r - y; else if (y > y1 - r) dy = y - (y1 - r);
      if (dx * dx + dy * dy <= r * r) put(x, y, c);
    }
  }
}

void draw_eye(int cx, int side, int open_pct, int gaze) {
  const EyeStyle& e = kEmotions[s_emotion].eye;
  int eyeW = static_cast<int>(e.width * S);
  int fullH = static_cast<int>(e.height * S);
  int open = fullH * e.openness / 100 * open_pct / 100;
  if (open < 6) open = 6;
  const int x0 = cx - eyeW / 2 + gaze, top = CY - open / 2;
  const int r = eyeW * 35 / 100;

  round_rect(x0 - 5, top - 5, eyeW + 10, open + 10, r + 5, C_GLOW);  // halo
  round_rect(x0, top, eyeW, open, r, C_EYE);

  const int lift = static_cast<int>(e.lift * S);
  if (lift > 0)  // lower-lid squint
    for (int y = top + open - lift; y < top + open; y++)
      for (int x = x0; x < x0 + eyeW; x++) put(x, y, C_BG);

  if (e.brow != 0) {  // slanted upper lid
    const int depthMax = open / 3;
    for (int i = 0; i < eyeW; i++) {
      const bool inner_deep = (e.brow > 0) == (side == 0);
      const int t = inner_deep ? i : eyeW - 1 - i;
      const int depth = depthMax * t / eyeW;
      for (int y = top - 3; y < top + depth; y++) put(x0 + i, y, C_BG);
    }
  }
}

void push() { esp_lcd_panel_draw_bitmap(s_panel, 0, 0, W, H, s_fb); }

void draw_eyes(int open_pct, int gaze) {
  clear();
  draw_eye(CX - GAP, 0, open_pct, gaze);
  draw_eye(CX + GAP, 1, open_pct, gaze);
  push();
}

void draw_char(int x0, int y0, int sc, char ch, uint16_t c) {
  const uint8_t* g = kFont[glyph_index(ch)];
  for (int r = 0; r < 5; r++)
    for (int col = 0; col < 3; col++)
      if (g[r] & (0b100 >> col))
        round_rect(x0 + col * sc, y0 + r * sc, sc, sc, 0, c);
}

void draw_text(const char* text) {
  clear();
  const int sc = 6, cell = sc * 4, cols = 9;
  // wrap into lines of <= cols chars, breaking on spaces
  char lines[5][12] = {{0}};
  int nlines = 0, len = 0;
  std::string word, cur;
  auto flush_word = [&](bool space) {
    if (cur.size() + word.size() > (size_t)cols && !cur.empty()) {
      strncpy(lines[nlines], cur.c_str(), 11); if (++nlines >= 5) return; cur.clear();
    }
    cur += word; if (space && !cur.empty() && cur.size() < (size_t)cols) cur += ' ';
    word.clear();
  };
  for (const char* p = text; *p; p++) {
    if (*p == ' ') flush_word(true); else word += *p;
  }
  flush_word(false);
  if (!cur.empty() && nlines < 5) strncpy(lines[nlines++], cur.c_str(), 11);
  (void)len;

  const int total_h = nlines * (cell + 6);
  int y = CY - total_h / 2;
  for (int l = 0; l < nlines; l++) {
    int n = strlen(lines[l]);
    int x = CX - (n * cell) / 2;
    for (int i = 0; i < n; i++) draw_char(x + i * cell, y, sc, lines[l][i], C_EYE);
    y += cell + 6;
  }
  push();
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
    if (was_saying || s_dirty) { was_saying = false; s_dirty = false; draw_eyes(100, gaze); }
    if (now >= next_blink) {
      draw_eyes(12, gaze);
      vTaskDelay(pdMS_TO_TICKS(75));
      draw_eyes(100, gaze);
      const int period = kEmotions[s_emotion].blink_period_ms;
      next_blink = now + period / 2 + esp_random() % period;
    }
    if (now >= next_saccade) {
      gaze = static_cast<int>(esp_random() % 21) - 10;
      draw_eyes(100, gaze);
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

  draw_eyes(100, 0);
  xTaskCreatePinnedToCore(face_task, "face", 4096, nullptr, 4, nullptr, 1);
}

}  // namespace buddy

#endif  // CONFIG_BUDDY_DISPLAY_GC9A01
