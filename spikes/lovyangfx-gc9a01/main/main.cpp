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
#include <cstdio>
#include "esp_random.h"

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

// LovyanGFX stores 16bpp sprites BYTE-SWAPPED (big-endian), because that is
// the order the SPI bus consumes — confirmed on hardware by the panel test
// card (row 2 native-endian rendered R/G/B as Blue/Red/Green; row 3 swapped
// was correct). Everything below computes in native RGB565 and converts only
// at the buffer boundary.
inline uint16_t to_store(uint16_t native) { return __builtin_bswap16(native); }
inline uint16_t from_store(uint16_t stored) { return __builtin_bswap16(stored); }

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
  p = to_store(blend565(from_store(p), c, a > 1.f ? 1.f : a));
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
    const uint16_t p = from_store(fb[y * W + x]);
    ESP_LOGI(TAG, "%s y=%3d raw=0x%04X r5=%2d g6=%2d b5=%2d", tag, y, p,
             (p >> 11) & 0x1F, (p >> 5) & 0x3F, p & 0x1F);
  }
}

void draw_eye(int cxi, int side, const Emotion& em, int open_pct, Variant v,
              int gx, int gy) {
  const EyeStyle& e = em.eye;
  const float eyeW = e.width * S;
  float open = e.height * S * (e.openness / 100.f) * (open_pct / 100.f);
  if (open < 6.f) open = 6.f;
  const float cx = cxi + gx, cy = CY + gy;
  const float hw = eyeW / 2, hh = open / 2;
  float r = eyeW * 0.42f;
  if (r > hh) r = hh;
  const float browAmt = e.brow != 0 ? open * 0.5f : 0.f;
  const float top = cy - hh;

  if (v == V_NATIVE) {
    spr.fillSmoothRoundRect(static_cast<int>(cx - hw), static_cast<int>(top),
                            static_cast<int>(eyeW), static_cast<int>(open),
                            static_cast<int>(r), spr.color565(em.r, em.g, em.b));
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
        if (x >= 0 && x < W && y >= 0 && y < H) fb[y * W + x] = to_store(col);
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

void draw_face(const Emotion& em, Variant v, int open_pct, int gx = 0, int gy = 0) {
  spr.fillScreen(TFT_BLACK);
  draw_eye(CX - GAP, 0, em, open_pct, v, gx, gy);
  draw_eye(CX + GAP, 1, em, open_pct, v, gx, gy);
  spr.setTextDatum(top_center);
  spr.setTextColor(TFT_WHITE);
  spr.setFont(&fonts::Font2);
  spr.drawString(kVariantName[v], 120, 188);
  spr.setFont(&fonts::Font0);
  spr.setTextColor(spr.color565(em.r, em.g, em.b));
  spr.drawString(em.name, 120, 210);
}

// Ship the whole framebuffer up the serial line as base64 so it can be turned
// back into a PNG on the laptop. Designing an eye by describing it over chat
// does not work; this lets the render be looked at directly.
void dump_fb(const char* label) {
  static const char* b64 =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  const uint8_t* p = reinterpret_cast<const uint8_t*>(fb);
  const size_t n = static_cast<size_t>(W) * H * 2;
  printf("\n---FB-BEGIN %s %d %d---\n", label, W, H);
  fflush(stdout);
  char line[125];
  int li = 0;
  for (size_t i = 0; i < n; i += 3) {
    const uint32_t b0 = p[i];
    const uint32_t b1 = (i + 1 < n) ? p[i + 1] : 0;
    const uint32_t b2 = (i + 2 < n) ? p[i + 2] : 0;
    const uint32_t v = (b0 << 16) | (b1 << 8) | b2;
    line[li++] = b64[(v >> 18) & 63];
    line[li++] = b64[(v >> 12) & 63];
    line[li++] = (i + 1 < n) ? b64[(v >> 6) & 63] : '=';
    line[li++] = (i + 2 < n) ? b64[v & 63] : '=';
    if (li >= 120) { line[li] = 0; printf("%s\n", line); li = 0; }
  }
  if (li) { line[li] = 0; printf("%s\n", line); }
  uint32_t sum = 0;
  for (size_t i = 0; i < n; i++) sum = sum * 31u + p[i];
  printf("---FB-END %08X---\n", (unsigned)sum);
  fflush(stdout);
}

// A few seconds of actual life: eased saccades plus blinks. A frozen pair of
// eyes reads as a logo; this is the only way to judge whether they read as a
// creature — and it shows the real frame rate under motion.
void animate(const Emotion& em, Variant v, int ms) {
  const int64_t t_end = esp_timer_get_time() + ms * 1000LL;
  int gx = 0, gy = 0, tx = 0, ty = 0;
  int64_t next_sacc = 0, next_blink = esp_timer_get_time() + 900000;
  int frames = 0;
  const int64_t t_start = esp_timer_get_time();

  while (esp_timer_get_time() < t_end) {
    const int64_t now = esp_timer_get_time();
    if (now >= next_sacc) {
      tx = static_cast<int>(esp_random() % 37) - 18;
      ty = static_cast<int>(esp_random() % 25) - 12;
      next_sacc = now + 500000 + (esp_random() % 1100000);
    }
    gx += (tx - gx) / 2;  // ease in, same shape as the firmware's face.look
    gy += (ty - gy) / 2;

    if (now >= next_blink) {
      for (int op : {45, 12, 45}) {
        draw_face(em, v, op, gx, gy);
        spr.pushSprite(0, 0);
        frames++;
      }
      const int period = em.blink_period_ms * 1000;
      next_blink = now + period / 2 + (esp_random() % period);
    }
    draw_face(em, v, 100, gx, gy);
    spr.pushSprite(0, 0);
    frames++;
  }
  const double secs = (esp_timer_get_time() - t_start) / 1e6;
  ESP_LOGI(TAG, "%-11s %-13s animated %5.1f fps (%d frames in %.1fs)",
           em.name, kVariantName[v], frames / secs, frames, secs);
}

// Panel test card. Answers one question: is the display faulty, or are we
// writing pixels in the wrong byte order?
//
// Three rows draw the SAME three colours by three different routes. Whichever
// row reads correctly as red / green / blue tells us the convention — and if
// ANY row is correct, the panel hardware is fine by definition.
void fill_raw(int x0, int y0, int w, int h, uint16_t v) {
  for (int y = y0; y < y0 + h; y++)
    for (int x = x0; x < x0 + w; x++)
      if (x >= 0 && x < W && y >= 0 && y < H) fb[y * W + x] = v;
}

void panel_test() {
  const struct { const char* n; uint8_t r, g, b; } cols[3] = {
      {"R", 255, 0, 0}, {"G", 0, 255, 0}, {"B", 0, 0, 255}};
  const int xs[3] = {45, 97, 149};
  const int SW = 46, SH = 30;

  spr.fillScreen(TFT_BLACK);
  spr.setTextDatum(top_center);
  spr.setTextColor(TFT_WHITE);
  spr.setFont(&fonts::Font0);

  for (int i = 0; i < 3; i++) spr.drawString(cols[i].n, xs[i] + SW / 2, 30);

  // Row 1: LovyanGFX's own API and its own colour type. If this row is right,
  // the panel, the wiring and the SPI bus are all fine.
  spr.drawString("1 LGFX color565", 120, 44);
  for (int i = 0; i < 3; i++)
    spr.fillRect(xs[i], 56, SW, SH, spr.color565(cols[i].r, cols[i].g, cols[i].b));

  // Row 2: direct buffer write, native little-endian RGB565 — what the eye
  // renderer does today.
  spr.drawString("2 RAW little-end", 120, 104);
  for (int i = 0; i < 3; i++)
    fill_raw(xs[i], 116, SW, SH, rgb(cols[i].r, cols[i].g, cols[i].b));

  // Row 3: direct buffer write, byte-swapped.
  spr.drawString("3 RAW byte-swap", 120, 164);
  for (int i = 0; i < 3; i++)
    fill_raw(xs[i], 176, SW, SH, __builtin_bswap16(rgb(cols[i].r, cols[i].g, cols[i].b)));

  spr.pushSprite(0, 0);

  ESP_LOGI(TAG, "PANEL TEST CARD");
  ESP_LOGI(TAG, "  row 1 = LovyanGFX API      row 2 = raw little-endian      row 3 = raw byte-swapped");
  ESP_LOGI(TAG, "  Report which row(s) show red, green, blue in that order.");
  ESP_LOGI(TAG, "  If row 1 is correct the panel is fine and this is purely a software convention bug.");

  // Rows 1 and 3 should be byte-identical. If they are, any tone difference
  // seen between them is the panel's vertical viewing angle (row 1 sits at
  // y=56, row 3 at y=176), not the drawing path — which matters, because the
  // eye renderer needs per-pixel buffer access and cannot use fillRect.
  for (int i = 0; i < 3; i++) {
    const uint16_t r1 = fb[(56 + SH / 2) * W + xs[i] + SW / 2];
    const uint16_t r3 = fb[(176 + SH / 2) * W + xs[i] + SW / 2];
    ESP_LOGI(TAG, "  %s row1=0x%04X row3=0x%04X -> %s", cols[i].n, r1, r3,
             r1 == r3 ? "IDENTICAL bytes" : "GENUINELY DIFFERENT");
  }
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
  // Diagnose the byte order before anything else — every other rendering
  // question downstream is meaningless until this is settled.
  panel_test();
  vTaskDelay(pdMS_TO_TICKS(8000));

  // Screenshot pass: one still of each variant, sent up the wire as PNG-able
  // base64 so the render can actually be looked at instead of described.
  // "neutral" and "angry" — angry because it is the one with the brow slant,
  // which is exactly what the library primitive cannot do.
  const int shot_emotions[] = {0, 5};  // neutral, angry
  for (int e : shot_emotions) {
    for (int v = 0; v < V_COUNT; v++) {
      char label[48];
      snprintf(label, sizeof label, "%s-%c", kEmotions[e].name, 'A' + v);
      draw_face(kEmotions[e], static_cast<Variant>(v), 100);
      spr.pushSprite(0, 0);
      dump_fb(label);
    }
  }
  ESP_LOGI(TAG, "screenshots done");

  // Then live: each variant animated with saccades and blinks, so the frame
  // rate under motion is visible rather than inferred from a still.
  for (;;) {
    for (int i = 0; i < kEmotionCount; i++) {
      for (int v = 0; v < V_COUNT; v++) {
        animate(kEmotions[i], static_cast<Variant>(v), 5000);
      }
    }
  }
}
