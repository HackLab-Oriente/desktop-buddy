// LovyanGFX spike — test 1: does the panel come up correct, and how fast?
//
// Answers, in order:
//   1. Does LovyanGFX build against ESP-IDF v6.0.2 at all? (build gate)
//   2. Colour order: is RED actually red, or does it come out blue?
//   3. Orientation: does text read forwards, or mirrored like esp_lcd did?
//   4. Frame time: how long does a full 240x240 push take vs our esp_lcd path?
//   5. Sprite cost: what does an offscreen 240x240 sprite do to PSRAM?
#include "lgfx_buddy.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "spike";
static LGFX_Buddy lcd;

namespace {

void report_mem(const char* when) {
  ESP_LOGI(TAG, "%-18s internal=%u B  psram=%u B", when,
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
}

// Time N full-screen pushes and report the average.
void bench_fullscreen(int n) {
  const int64_t t0 = esp_timer_get_time();
  for (int i = 0; i < n; i++) lcd.fillScreen(i & 1 ? TFT_BLACK : TFT_NAVY);
  const int64_t dt = esp_timer_get_time() - t0;
  ESP_LOGI(TAG, "fillScreen x%d: %.2f ms/frame (%.1f fps)", n,
           dt / 1000.0 / n, 1e6 * n / (double)dt);
}

}  // namespace

extern "C" void app_main() {
  report_mem("boot");

  lcd.init();
  lcd.setBrightness(160);
  ESP_LOGI(TAG, "panel up: %dx%d", lcd.width(), lcd.height());
  report_mem("after lcd.init");

  // --- Test 2: colour order -------------------------------------------------
  // Watch the screen. The label must MATCH the colour you see. If RED shows
  // blue, flip cfg.rgb_order in lgfx_buddy.h. If everything is a photo
  // negative, flip cfg.invert.
  const struct { const char* name; uint16_t col; uint16_t text; } swatches[] = {
      {"RED", TFT_RED, TFT_WHITE},
      {"GREEN", TFT_GREEN, TFT_BLACK},
      {"BLUE", TFT_BLUE, TFT_WHITE},
      {"WHITE", TFT_WHITE, TFT_BLACK},
  };
  for (auto& s : swatches) {
    lcd.fillScreen(s.col);
    lcd.setTextColor(s.text, s.col);
    lcd.setTextDatum(middle_center);
    lcd.setTextSize(3);
    lcd.drawString(s.name, lcd.width() / 2, lcd.height() / 2);
    ESP_LOGI(TAG, "showing %s", s.name);
    vTaskDelay(pdMS_TO_TICKS(1200));
  }

  // --- Test 3: orientation + text quality -----------------------------------
  // "BUDDY" must read left-to-right. Our esp_lcd path came up mirrored and
  // needed esp_lcd_panel_mirror(panel, true, false).
  lcd.fillScreen(TFT_BLACK);
  lcd.setTextColor(TFT_CYAN, TFT_BLACK);
  lcd.setTextDatum(middle_center);
  lcd.setTextSize(1);
  lcd.setFont(&fonts::Font4);
  lcd.drawString("BUDDY", 120, 70);
  lcd.setFont(&fonts::Font2);
  lcd.drawString("if this reads backwards,", 120, 110);
  lcd.drawString("set offset_rotation", 120, 130);
  // A small-text sample: this is the readability our 5x7 font has to beat.
  lcd.setFont(&fonts::Font0);
  lcd.drawString("the quick brown fox jumps over", 120, 165);
  lcd.drawString("0123456789 !?,.'-:", 120, 180);
  vTaskDelay(pdMS_TO_TICKS(4000));

  // --- Test 4: how fast is a full frame? ------------------------------------
  bench_fullscreen(60);

  // --- Test 5: what does an offscreen sprite cost? --------------------------
  // This is the "fly around the buddy" primitive and the surface we'd render
  // the SDF eyes into. 240*240*2 = 112 KB, same as our current framebuffer.
  report_mem("before sprite");
  LGFX_Sprite fb(&lcd);
  fb.setPsram(true);
  fb.setColorDepth(16);
  if (!fb.createSprite(240, 240)) {
    ESP_LOGE(TAG, "createSprite(240,240) FAILED — not enough PSRAM?");
  } else {
    report_mem("after sprite");
    const int64_t t0 = esp_timer_get_time();
    for (int i = 0; i < 60; i++) {
      fb.fillScreen(TFT_BLACK);
      fb.fillSmoothCircle(90, 120, 26, TFT_CYAN);
      fb.fillSmoothCircle(150, 120, 26, TFT_CYAN);
      fb.pushSprite(0, 0);
    }
    const int64_t dt = esp_timer_get_time() - t0;
    ESP_LOGI(TAG, "sprite draw+push x60: %.2f ms/frame (%.1f fps)",
             dt / 1000.0 / 60, 60e6 / (double)dt);
  }

  ESP_LOGI(TAG, "spike done — screen holds the last frame");
  for (;;) vTaskDelay(pdMS_TO_TICKS(1000));
}
