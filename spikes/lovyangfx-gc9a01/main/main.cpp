// LovyanGFX spike — test 2: the eye showcase.
//
// Renders the SAME emotion five ways, cycling with an on-screen label, so the
// lab can look at them and pick. These are options, not a proposal that one is
// better — the eye look is a group decision.
//
//   A SHIPPED    exactly what round_face.cpp draws today: flat colour + glow.
//   B DITHERED   + vertical gradient, ordered-dithered so RGB565 stops banding.
//                (The banding is *why* the gradient was removed originally.)
//   C CATCHLIGHT + a specular highlight. In character animation this is the
//                single biggest "alive" cue — it reads as a wet eye rather
//                than a painted shape. Cozmo/Vector/EMO all have one.
//   D DEPTH      + inner rim shading, so the eye reads as a lens not a decal.
//   E LGFX NATIVE  fillSmoothRoundRect — what the library gives for free.
//                Deliberately shows the limitation: it CANNOT do the brow
//                slant or the happy squint, because those are carved out of
//                the shape as coverage, not drawn as shapes.
//
// Serial log separates draw time (our CPU cost) from push time (SPI wire
// cost), because the earlier benchmark showed we are wire-bound.
#include "lgfx_buddy.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <cmath>

static const char* TAG = "eyes";
static LGFX_Buddy lcd;
static LGFX_Sprite spr(&lcd);
static uint16_t* fb = nullptr;

// ===== emotion model — copied verbatim from firmware/.../face_model.cpp =====
struct EyeStyle { int width, height, openness, lift, brow; };
struct Emotion { const char* name; EyeStyle eye; int blink_period_ms; uint8_t r, g, b; };

static const Emotion kEmotions[] = {
    {"neutral",    {26, 30, 100, 0,  0},  3800,   0, 190, 255},
    {"happy",      {26, 30, 100, 14, 0},  3000,  40, 235, 120},
    {"curious",    {30, 34, 100, 0,  0},  2600,   0, 210, 235},
    {"sleepy",     {26, 30, 35,  0,  0},  6000,  90,  90, 200},
    {"surprised",  {34, 40, 100, 0,  0},  5000, 150, 240, 255},
    {"angry",      {28, 28, 100, 0,  1},  3200, 255,  50,  25},
    {"sad",        {24, 26, 75,  0, -1},  5200,  50, 110, 255},
    {"suspicious", {26, 30, 55,  0,  1},  2200, 255, 180,  20},
};
static const int kEmotionCount = sizeof(kEmotions) / sizeof(kEmotions[0]);

// ===== geometry, copied from round_face.cpp =====
constexpr int W = 240, H = 240;
constexpr float S = 2.6f;
constexpr int CX = 120, CY = 120, GAP = 42;

enum Variant { V_SHIPPED = 0, V_DITHER, V_CATCH, V_DEPTH, V_NATIVE, V_COUNT };
static const char* kVariantName[] = {"A SHIPPED", "B DITHERED", "C CATCHLIGHT",
                                     "D DEPTH", "E LGFX NATIVE"};

inline float clampf(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }

constexpr uint16_t rgb(int r, int g, int b) {
  return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

// Ordered dither. Banding in RGB565 comes from truncating 8-bit channels to
// 5/6/5 — we throw away 3 bits of red/blue and 2 of green. Nudging each pixel
// by a Bayer-patterned fraction of one lost step before truncation turns the
// hard bands into a fine stipple the eye reads as a smooth ramp.
static const uint8_t kBayer[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};
inline uint16_t rgb_dither(int x, int y, float rf, float gf, float bf) {
  // 1.5 quantisation steps of amplitude: one step barely breaks a band on a
  // 240 px panel viewed this close, 1.5 diffuses it properly.
  const float t = (kBayer[((y & 3) << 2) | (x & 3)] - 7.5f) / 15.0f;   // -0.5..+0.5
  const int r = static_cast<int>(clampf(rf + t * 12.0f, 0.f, 255.f));  // 3 bits lost
  const int g = static_cast<int>(clampf(gf + t * 6.0f, 0.f, 255.f));   // 2 bits lost
  const int b = static_cast<int>(clampf(bf + t * 12.0f, 0.f, 255.f));
  return rgb(r, g, b);
}

inline uint16_t blend565(uint16_t bg, uint16_t fg, float a) {
  const int br = (bg >> 11) & 0x1F, bgg = (bg >> 5) & 0x3F, bb = bg & 0x1F;
  const int fr = (fg >> 11) & 0x1F, fgg = (fg >> 5) & 0x3F, fbb = fg & 0x1F;
  const int r = br + static_cast<int>((fr - br) * a + 0.5f);
  const int g = bgg + static_cast<int>((fgg - bgg) * a + 0.5f);
  const int b = bb + static_cast<int>((fbb - bb) * a + 0.5f);
  return static_cast<uint16_t>((r << 11) | (g << 5) | b);
}
inline void blend_at(int x, int y, uint16_t c, float a) {
  if (x < 0 || x >= W || y < 0 || y >= H || a <= 0.f) return;
  uint16_t& p = fb[y * W + x];
  p = blend565(p, c, a > 1.f ? 1.f : a);
}

// Kept for reference — the shipped version. The inner loop below inlines this
// with the per-row term hoisted out and the sqrt skipped except at corners.
inline float sd_round_rect(float px, float py, float cx, float cy,
                           float hw, float hh, float r) {
  const float qx = fabsf(px - cx) - (hw - r);
  const float qy = fabsf(py - cy) - (hh - r);
  const float ax = qx > 0 ? qx : 0, ay = qy > 0 ? qy : 0;
  const float outside = sqrtf(ax * ax + ay * ay);
  const float inside = fminf(fmaxf(qx, qy), 0.f);
  return outside + inside - r;
}

// Diagnostic: dump a vertical scan through the centre of the left eye so we
// can see what is actually in the buffer, rather than guessing from the panel.
void dump_column(const char* tag) {
  const int x = CX - GAP;
  for (int y = 76; y <= 164; y += 6) {
    const uint16_t p = fb[y * W + x];
    ESP_LOGI(TAG, "%s y=%3d raw=0x%04X r5=%2d g6=%2d b5=%2d", tag, y, p,
             (p >> 11) & 0x1F, (p >> 5) & 0x3F, p & 0x1F);
  }
}

void draw_eye(int cxi, int side, const Emotion& em, int open_pct, Variant v) {
  const EyeStyle& e = em.eye;
  const float eyeW = e.width * S;
  float open = e.height * S * (e.openness / 100.f) * (open_pct / 100.f);
  if (open < 6.f) open = 6.f;
  const float cx = cxi, cy = CY;
  const float hw = eyeW / 2, hh = open / 2;
  float r = eyeW * 0.42f;
  if (r > hh) r = hh;
  const float browAmt = e.brow != 0 ? open * 0.5f : 0.f;
  const float top = cy - hh;

  if (v == V_NATIVE) {
    spr.fillSmoothRoundRect(static_cast<int>(cx - hw), static_cast<int>(top),
                            static_cast<int>(eyeW), static_cast<int>(open),
                            static_cast<int>(r), rgb(em.r, em.g, em.b));
    return;
  }

  const int gm = 10;  // glow margin
  const int x0 = static_cast<int>(cx - hw) - gm, x1 = static_cast<int>(cx + hw) + gm;
  const int y0 = static_cast<int>(top) - gm, y1 = static_cast<int>(cy + hh) + gm;

  // Catchlight sits upper-left on both eyes — a single light source. Making
  // it symmetric per-eye is the classic mistake; it kills the illusion.
  const float lx = cx - hw * 0.34f, ly = cy - hh * 0.45f;
  const float lr = eyeW * 0.12f;
  const float span = open < 1.f ? 1.f : open;
  // Optimisation 4: these are constant per eye — they were being recomputed
  // for every pixel.
  const uint16_t glow_col = rgb(em.r / 4, em.g / 4, em.b / 4);
  const uint16_t base_col = rgb(em.r, em.g, em.b);

  // Optimisation 2 & 3: qy depends only on the row, so hoist it. And the sqrt
  // is only needed at the four rounded corners, where qx AND qy are both
  // positive — everywhere else it was computing sqrtf(0) or sqrtf(v*v).
  const float ihw = hw - r, ihh = hh - r;
  for (int y = y0; y <= y1; y++) {
    const float qy = fabsf(y + 0.5f - cy) - ihh;
    const float ay = qy > 0.f ? qy : 0.f;
    const float ay2 = ay * ay;
    for (int x = x0; x <= x1; x++) {
      const float qx = fabsf(x + 0.5f - cx) - ihw;
      float outside;
      if (qx > 0.f) {
        outside = (ay > 0.f) ? sqrtf(qx * qx + ay2) : qx;
      } else {
        outside = ay;
      }
      const float d = outside + fminf(fmaxf(qx, qy), 0.f) - r;
      if (d > 7.f) continue;  // beyond even the glow — nothing to draw

      float ecov = clampf(0.5f - d, 0.f, 1.f);
      const float gcov = clampf((7.f - d) / 7.f, 0.f, 1.f) * 0.5f;

      if (ecov > 0.f) {
        if (browAmt > 0.f) {
          const float t = clampf((x + 0.5f - (cx - hw)) / eyeW, 0.f, 1.f);
          const bool deep_right = (e.brow > 0) == (side == 0);
          const float frac = deep_right ? t : 1.f - t;
          const float cut_y = top + browAmt * frac;
          ecov *= clampf((y + 0.5f) - cut_y + 0.5f, 0.f, 1.f);
        }
        if (e.lift > 0) {
          const float bot_y = cy + hh - e.lift * S;
          ecov *= clampf(bot_y - (y + 0.5f) + 0.5f, 0.f, 1.f);
        }
      }

      uint16_t col = 0;
      if (ecov > 0.f) {
        if (v == V_SHIPPED) {
          col = base_col;  // flat, exactly as shipped
        } else {
          const float t = clampf(((y + 0.5f) - top) / span, 0.f, 1.f);
          // Never exceed 1.0. Scaling a channel that is already at 255 (which
          // neutral's blue is) just clips, flat-topping the ramp — that is the
          // bug that made the gradient invisible on the panel.
          float k = 1.0f - 0.55f * t;
          if (v == V_DEPTH) {
            const float rim = clampf((-d) / 6.f, 0.f, 1.f);  // 0 at edge, 1 inside
            k *= 0.72f + 0.28f * rim;
          }
          col = rgb_dither(x, y, em.r * k, em.g * k, em.b * k);
        }
      }

      // Optimisation 5: where the eye is fully opaque the glow behind it is
      // invisible and there is nothing to blend against, so skip the whole
      // read-modify-write. That is the majority of the eye's pixels.
      if (ecov >= 0.999f) {
        if (x >= 0 && x < W && y >= 0 && y < H) fb[y * W + x] = col;
      } else {
        if (gcov > 0.f) blend_at(x, y, glow_col, gcov);
        if (ecov > 0.f) blend_at(x, y, col, ecov);
      }

      if ((v == V_CATCH || v == V_DEPTH) && ecov > 0.f) {
        const float dx = x + 0.5f - lx, dy = y + 0.5f - ly;
        const float dl = sqrtf(dx * dx + dy * dy) - lr;
        const float ccov = clampf(0.5f - dl, 0.f, 1.f) * ecov;
        if (ccov > 0.f) blend_at(x, y, rgb(255, 255, 255), ccov * 0.92f);
      }
    }
  }
}

void draw_face(const Emotion& em, Variant v, int open_pct) {
  spr.fillScreen(TFT_BLACK);
  draw_eye(CX - GAP, 0, em, open_pct, v);
  draw_eye(CX + GAP, 1, em, open_pct, v);
  spr.setTextDatum(top_center);
  spr.setTextColor(TFT_WHITE);
  spr.setFont(&fonts::Font2);
  spr.drawString(kVariantName[v], 120, 188);
  spr.setFont(&fonts::Font0);
  spr.setTextColor(rgb(em.r, em.g, em.b));
  spr.drawString(em.name, 120, 210);
}

extern "C" void app_main() {
  lcd.init();
  lcd.setBrightness(160);

  spr.setColorDepth(16);
  // Try internal RAM first — it is ~2x faster to draw into than PSRAM, and
  // 115 KB out of ~380 KB free is affordable. Fall back if it will not fit.
  spr.setPsram(false);
  bool internal = spr.createSprite(W, H);
  if (!internal) {
    spr.setPsram(true);
    if (!spr.createSprite(W, H)) {
      ESP_LOGE(TAG, "could not allocate a 240x240 sprite at all");
      return;
    }
  }
  fb = static_cast<uint16_t*>(spr.getBuffer());
  ESP_LOGI(TAG, "sprite in %s RAM; free internal=%u psram=%u",
           internal ? "INTERNAL" : "PSRAM",
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
  // Diagnostic pass: draw neutral flat (A) and gradient (B), and dump the same
  // column from each. If B's numbers do not ramp, the gradient never reached
  // the buffer; if the channels look wrong, we have a colour-order problem.
  draw_face(kEmotions[0], V_SHIPPED, 100);
  dump_column("A-flat  ");
  draw_face(kEmotions[0], V_DITHER, 100);
  dump_column("B-grad  ");
  ESP_LOGI(TAG, "expected for neutral (0,190,255): r5=0 g6=~47 b5=~31 at the top,"
                " falling to about g6=26 b5=17 at the bottom");

  ESP_LOGI(TAG, "%-11s %-13s %8s %8s", "emotion", "variant", "draw_ms", "push_ms");

  for (;;) {
    for (int i = 0; i < kEmotionCount; i++) {
      for (int v = 0; v < V_COUNT; v++) {
        const int64_t t0 = esp_timer_get_time();
        draw_face(kEmotions[i], static_cast<Variant>(v), 100);
        const int64_t t1 = esp_timer_get_time();
        spr.pushSprite(0, 0);
        const int64_t t2 = esp_timer_get_time();
        ESP_LOGI(TAG, "%-11s %-13s %8.2f %8.2f", kEmotions[i].name,
                 kVariantName[v], (t1 - t0) / 1000.0, (t2 - t1) / 1000.0);
        vTaskDelay(pdMS_TO_TICKS(2200));
      }
    }
  }
}
