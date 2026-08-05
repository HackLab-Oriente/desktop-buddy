// LovyanGFX spike — test 4: can we have the expressive shapes AND the speed?
//
// Feedback from the lab: A/B's expressiveness is wanted, E's fluidity is
// wanted, the catchlight is not. So the catchlight and rim variants are gone
// and two new approaches are in, both aimed at the same target — B's look at
// E's frame rate.
//
//   A SHIPPED    what round_face.cpp draws today: flat colour + glow. Baseline.
//   B GRADIENT   dithered vertical gradient + glow. The preferred look.
//   C CACHED     pixel-identical to B, but the SDF runs only when the emotion
//                or blink state changes. A saccade is a *translation* of an
//                unchanged image, so it costs a blit, not a re-render.
//   D PRIMITIVE  LovyanGFX shapes only: fillSmoothRoundRect for the eye, then
//                a black triangle to OCCLUDE the brow and a black rect for the
//                squint. Answers "can't we get those shapes another way?" —
//                yes, by subtracting rather than by carving coverage.
#include "lgfx_buddy.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>

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

constexpr int W = 240, H = 240;
constexpr float S = 2.6f;
constexpr int CX = 120, CY = 120, GAP = 42;

enum Variant { V_SHIPPED = 0, V_GRADIENT, V_CACHED, V_PRIM, V_COUNT };
static const char* kVariantName[] = {"A SHIPPED", "B GRADIENT", "C CACHED", "D PRIMITIVE"};

// The three openness levels a blink passes through. Cached variants
// pre-render these, so a blink costs three blits instead of three renders.
static const int kLevels[] = {100, 45, 12};
static const int kLevelCount = 3;

inline float clampf(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }

constexpr uint16_t rgb(int r, int g, int b) {
  return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

// LovyanGFX stores 16bpp sprites BIG-endian (SPI byte order). Confirmed on
// hardware by the panel test card. Convert only at the buffer boundary.
inline uint16_t to_store(uint16_t native) { return __builtin_bswap16(native); }
inline uint16_t from_store(uint16_t stored) { return __builtin_bswap16(stored); }

static const uint8_t kBayer[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};
inline uint16_t rgb_dither(int x, int y, float rf, float gf, float bf) {
  const float t = (kBayer[((y & 3) << 2) | (x & 3)] - 7.5f) / 15.0f;
  const int r = static_cast<int>(clampf(rf + t * 12.0f, 0.f, 255.f));
  const int g = static_cast<int>(clampf(gf + t * 6.0f, 0.f, 255.f));
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
  p = to_store(blend565(from_store(p), c, a > 1.f ? 1.f : a));
}

// ===== the SDF renderer (A and B, and what C caches) =====
void draw_eye_sdf(int cxi, int side, const Emotion& em, int open_pct,
                  bool gradient, int gx, int gy) {
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

  const int gm = 10;
  const int x0 = static_cast<int>(cx - hw) - gm, x1 = static_cast<int>(cx + hw) + gm;
  const int y0 = static_cast<int>(top) - gm, y1 = static_cast<int>(cy + hh) + gm;
  const float span = open < 1.f ? 1.f : open;
  const uint16_t glow_col = rgb(em.r / 4, em.g / 4, em.b / 4);
  const uint16_t base_col = rgb(em.r, em.g, em.b);
  const float ihw = hw - r, ihh = hh - r;

  for (int y = y0; y <= y1; y++) {
    const float qy = fabsf(y + 0.5f - cy) - ihh;
    const float ay = qy > 0.f ? qy : 0.f;
    const float ay2 = ay * ay;
    for (int x = x0; x <= x1; x++) {
      const float qx = fabsf(x + 0.5f - cx) - ihw;
      float outside;
      if (qx > 0.f) outside = (ay > 0.f) ? sqrtf(qx * qx + ay2) : qx;
      else outside = ay;
      const float d = outside + fminf(fmaxf(qx, qy), 0.f) - r;
      if (d > 7.f) continue;

      float ecov = clampf(0.5f - d, 0.f, 1.f);
      const float gcov = clampf((7.f - d) / 7.f, 0.f, 1.f) * 0.5f;
      if (ecov > 0.f) {
        if (browAmt > 0.f) {
          const float t = clampf((x + 0.5f - (cx - hw)) / eyeW, 0.f, 1.f);
          const bool deep_right = (e.brow > 0) == (side == 0);
          const float frac = deep_right ? t : 1.f - t;
          ecov *= clampf((y + 0.5f) - (top + browAmt * frac) + 0.5f, 0.f, 1.f);
        }
        if (e.lift > 0) {
          const float bot_y = cy + hh - e.lift * S;
          ecov *= clampf(bot_y - (y + 0.5f) + 0.5f, 0.f, 1.f);
        }
      }

      uint16_t col = base_col;
      if (ecov > 0.f && gradient) {
        const float t = clampf(((y + 0.5f) - top) / span, 0.f, 1.f);
        const float k = 1.0f - 0.55f * t;
        col = rgb_dither(x, y, em.r * k, em.g * k, em.b * k);
      }
      if (ecov >= 0.999f) {
        if (x >= 0 && x < W && y >= 0 && y < H) fb[y * W + x] = to_store(col);
      } else {
        if (gcov > 0.f) blend_at(x, y, glow_col, gcov);
        if (ecov > 0.f) blend_at(x, y, col, ecov);
      }
    }
  }
}

// ===== D PRIMITIVE: shapes + occlusion, no per-pixel maths =====
// The brow is not carved out of coverage here — the whole eye is drawn, then
// a black triangle is painted over the part the brow removes. Subtraction
// instead of coverage multiplication. Same idea for the squint, with a rect.
void draw_eye_prim(int cxi, int side, const Emotion& em, int open_pct, int gx, int gy) {
  const EyeStyle& e = em.eye;
  const float eyeW = e.width * S;
  float open = e.height * S * (e.openness / 100.f) * (open_pct / 100.f);
  if (open < 6.f) open = 6.f;
  const int cx = cxi + gx, cy = CY + gy;
  const int hw = static_cast<int>(eyeW / 2), hh = static_cast<int>(open / 2);
  int r = static_cast<int>(eyeW * 0.42f);
  if (r > hh) r = hh;
  const int left = cx - hw, top = cy - hh;

  spr.fillSmoothRoundRect(left, top, hw * 2, hh * 2, r,
                          spr.color565(em.r, em.g, em.b));

  if (e.brow != 0) {
    const int browAmt = static_cast<int>(open * 0.5f);
    const bool deep_right = (e.brow > 0) == (side == 0);
    // Overshoot upward by a few px so the triangle also removes the eye's
    // anti-aliased top edge, not just its interior.
    const int t2 = top - 6;
    if (deep_right)
      spr.fillTriangle(left - 4, t2, cx + hw + 4, t2, cx + hw + 4, top + browAmt,
                       TFT_BLACK);
    else
      spr.fillTriangle(cx + hw + 4, t2, left - 4, t2, left - 4, top + browAmt,
                       TFT_BLACK);
  }
  if (e.lift > 0) {
    const int bot = cy + hh - static_cast<int>(e.lift * S);
    spr.fillRect(left - 4, bot, hw * 2 + 8, cy + hh - bot + 6, TFT_BLACK);
  }
}

void draw_label(const Emotion& em, Variant v) {
  spr.setTextDatum(top_center);
  spr.setTextColor(TFT_WHITE);
  spr.setFont(&fonts::Font2);
  spr.drawString(kVariantName[v], 120, 188);
  spr.setFont(&fonts::Font0);
  spr.setTextColor(spr.color565(em.r, em.g, em.b));
  spr.drawString(em.name, 120, 210);
}

// ===== C CACHED: render the SDF once per (emotion, openness), then blit =====
static LGFX_Sprite cache[kLevelCount] = {LGFX_Sprite(&lcd), LGFX_Sprite(&lcd),
                                         LGFX_Sprite(&lcd)};
static int cached_emotion = -1;

void build_cache(int emo) {
  if (cached_emotion == emo) return;
  const int64_t t0 = esp_timer_get_time();
  for (int i = 0; i < kLevelCount; i++) {
    spr.fillScreen(TFT_BLACK);
    draw_eye_sdf(CX - GAP, 0, kEmotions[emo], kLevels[i], true, 0, 0);
    draw_eye_sdf(CX + GAP, 1, kEmotions[emo], kLevels[i], true, 0, 0);
    // Copy the finished frame into the cache sprite.
    memcpy(cache[i].getBuffer(), spr.getBuffer(), static_cast<size_t>(W) * H * 2);
  }
  cached_emotion = emo;
  ESP_LOGI(TAG, "cache rebuilt for %-11s in %.1f ms (one-off per emotion)",
           kEmotions[emo].name, (esp_timer_get_time() - t0) / 1000.0);
}

void draw_face(const Emotion& em, int emo_idx, Variant v, int open_pct, int gx, int gy) {
  if (v == V_CACHED) {
    build_cache(emo_idx);
    int li = 0;  // pick the nearest cached openness level
    for (int i = 1; i < kLevelCount; i++)
      if (abs(kLevels[i] - open_pct) < abs(kLevels[li] - open_pct)) li = i;
    spr.fillScreen(TFT_BLACK);
    cache[li].pushSprite(&spr, gx, gy, TFT_BLACK);  // black = transparent
    draw_label(em, v);
    return;
  }
  spr.fillScreen(TFT_BLACK);
  if (v == V_PRIM) {
    draw_eye_prim(CX - GAP, 0, em, open_pct, gx, gy);
    draw_eye_prim(CX + GAP, 1, em, open_pct, gx, gy);
  } else {
    const bool grad = (v == V_GRADIENT);
    draw_eye_sdf(CX - GAP, 0, em, open_pct, grad, gx, gy);
    draw_eye_sdf(CX + GAP, 1, em, open_pct, grad, gx, gy);
  }
  draw_label(em, v);
}

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
    const uint32_t b0 = p[i], b1 = (i + 1 < n) ? p[i + 1] : 0, b2 = (i + 2 < n) ? p[i + 2] : 0;
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
  printf("---FB-END %08X---\n", static_cast<unsigned>(sum));
  fflush(stdout);
}

void animate(int emo, Variant v, int ms) {
  const Emotion& em = kEmotions[emo];
  const int64_t t_start = esp_timer_get_time(), t_end = t_start + ms * 1000LL;
  int gx = 0, gy = 0, tx = 0, ty = 0, frames = 0;
  int64_t next_sacc = 0, next_blink = t_start + 900000;

  while (esp_timer_get_time() < t_end) {
    const int64_t now = esp_timer_get_time();
    if (now >= next_sacc) {
      tx = static_cast<int>(esp_random() % 37) - 18;
      ty = static_cast<int>(esp_random() % 25) - 12;
      next_sacc = now + 500000 + (esp_random() % 1100000);
    }
    gx += (tx - gx) / 2;
    gy += (ty - gy) / 2;
    if (now >= next_blink) {
      for (int li = 1; li < kLevelCount; li++) {
        draw_face(em, emo, v, kLevels[li], gx, gy);
        spr.pushSprite(0, 0);
        frames++;
      }
      const int period = em.blink_period_ms * 1000;
      next_blink = now + period / 2 + (esp_random() % period);
    }
    draw_face(em, emo, v, 100, gx, gy);
    spr.pushSprite(0, 0);
    frames++;
  }
  const double secs = (esp_timer_get_time() - t_start) / 1e6;
  ESP_LOGI(TAG, "%-11s %-12s %5.1f fps", em.name, kVariantName[v], frames / secs);
}


// ===== the "something is bothering the buddy" demo =====
// A sprite wanders the screen; the eyes track it via the same gaze offset the
// firmware's face.look uses; when it gets too close the buddy flinches.
// The placeholder below is swapped for the real HackLab logo once we have the
// asset — it becomes an RGB565 + 8-bit alpha array, composited with our own
// blend so the edges stay soft instead of being hard-keyed.
constexpr int IW = 56, IH = 56;
static LGFX_Sprite intruder(&lcd);

void build_intruder() {
  intruder.setColorDepth(16);
  intruder.setPsram(true);
  intruder.createSprite(IW, IH);
  intruder.fillScreen(TFT_BLACK);  // black is the transparency key
  intruder.fillSmoothCircle(IW / 2, IH / 2, 25, intruder.color565(250, 205, 60));
  intruder.fillSmoothCircle(IW / 2, IH / 2, 19, intruder.color565(18, 18, 22));
  intruder.setTextDatum(middle_center);
  intruder.setTextColor(intruder.color565(250, 205, 60));
  intruder.setFont(&fonts::Font2);
  intruder.drawString("HL", IW / 2, IH / 2 + 1);
}

void demo_intruder(int seconds) {
  float ix = 40, iy = 40, vx = 1.7f, vy = 1.1f;
  int gx = 0, gy = 0, frames = 0, emo = 0, flinch = 0;
  const int64_t t0 = esp_timer_get_time(), t_end = t0 + seconds * 1000000LL;

  while (esp_timer_get_time() < t_end) {
    ix += vx; iy += vy;
    if (ix < 26 || ix > W - 26) vx = -vx;
    if (iy < 26 || iy > H - 26) vy = -vy;

    // Eyes track it: same units as the firmware's face.look (-100..100 scaled).
    const int tx = static_cast<int>((ix - CX) * 0.22f);
    const int ty = static_cast<int>((iy - CY) * 0.16f);
    gx += (tx - gx) / 3;
    gy += (ty - gy) / 3;

    // Too close to an eye -> flinch. This is the whole point: the buddy is
    // not a screensaver, it reacts to something in its world.
    const float dl = fabsf(ix - (CX - GAP)) + fabsf(iy - CY);
    const float dr = fabsf(ix - (CX + GAP)) + fabsf(iy - CY);
    const bool close = (dl < 46 || dr < 46);
    if (close && flinch == 0) { flinch = 12; emo = 4; }   // surprised
    if (flinch > 0 && --flinch == 0) emo = 0;             // back to neutral

    draw_face(kEmotions[emo], emo, V_CACHED, flinch > 6 ? 45 : 100, gx, gy);
    intruder.pushSprite(&spr, static_cast<int>(ix) - IW / 2,
                        static_cast<int>(iy) - IH / 2, TFT_BLACK);
    spr.pushSprite(0, 0);
    frames++;
  }
  ESP_LOGI(TAG, "intruder demo: %.1f fps (eyes tracking + sprite composited)",
           frames / ((esp_timer_get_time() - t0) / 1e6));
}

extern "C" void app_main() {
  lcd.init();
  lcd.setBrightness(160);

  spr.setColorDepth(16);
  spr.setPsram(false);
  if (!spr.createSprite(W, H)) { spr.setPsram(true); spr.createSprite(W, H); }
  fb = static_cast<uint16_t*>(spr.getBuffer());
  for (int i = 0; i < kLevelCount; i++) {
    cache[i].setColorDepth(16);
    cache[i].setPsram(true);  // 3 x 115 KB — PSRAM, they are only ever blitted
    if (!cache[i].createSprite(W, H)) ESP_LOGE(TAG, "cache sprite %d failed", i);
  }
  ESP_LOGI(TAG, "free internal=%u psram=%u",
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

  // Stills for inspection: neutral (plain) and angry (the brow slant, which is
  // the shape D PRIMITIVE has to reproduce by subtraction).
  for (int e : {0, 5}) {
    for (int v = 0; v < V_COUNT; v++) {
      char label[48];
      snprintf(label, sizeof label, "%s-%c", kEmotions[e].name, 'A' + v);
      draw_face(kEmotions[e], e, static_cast<Variant>(v), 100, 0, 0);
      spr.pushSprite(0, 0);
      dump_fb(label);
    }
  }
  ESP_LOGI(TAG, "screenshots done");

  build_intruder();
  demo_intruder(6);
  for (int e : {0, 5}) { (void)e; }
  draw_face(kEmotions[0], 0, V_CACHED, 100, 8, 6);
  intruder.pushSprite(&spr, 150, 60, TFT_BLACK);
  spr.pushSprite(0, 0);
  dump_fb("intruder");

  ESP_LOGI(TAG, "--- animated frame rates ---");
  for (;;)
    for (int i = 0; i < kEmotionCount; i++)
      for (int v = 0; v < V_COUNT; v++) animate(i, static_cast<Variant>(v), 5000);
}
