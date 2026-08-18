// GC9A01 240x240 round color face — the buddy's face.
//
// Two layers, which is the arrangement the LovyanGFX spike settled on:
//   * the EYES are ours — a signed-distance-field renderer, because the brow
//     slant and the happy squint are carved out of the shape as coverage, not
//     drawn as shapes, and no library primitive can express that;
//   * everything ELSE is LovyanGFX — panel driver, sprites, fonts, blitting.
//
// The eyes are CACHED. The SDF costs 40-100 ms per frame, but the eye image
// only changes on a blink or an emotion change; a saccade is a *translation*
// of an unchanged image. Rendering once per (emotion, openness) and blitting
// at a gaze offset took the spike from 13 fps to ~30, with identical pixels.
// See spikes/lovyangfx-gc9a01/README.md for the measurements.
#include "sdkconfig.h"

#include "lgfx_buddy.h"
#include "logo_hacklab.h"

#include "bus.h"
#include "expressions.h"
#include "font_latin.h"
#include "latin1.h"
#include "face_model.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "face";

namespace buddy {
namespace {

constexpr int W = 240, H = 240;
constexpr float S = 2.6f;                    // model(128x64) -> panel scale
constexpr int CX = 120, CY = 120, GAP = 42;  // eye centers at CX±GAP

// How deep the angled upper lid cuts, as a fraction of eye height. This was
// 0.5 for a long time, which removed half the eye and left wedges rather than
// angry eyes — the reason angry/sad/suspicious looked broken since the PoC.
constexpr float kBrowDepth = 0.24f;

// Openness levels a blink passes through. Cached, so a blink is three blits.
constexpr int kLevels[] = {100, 45, 12};
constexpr int kLevelCount = 3;

// --- how the frame gets built, per target ---------------------------------
// The S3 draws the whole 240x240 frame at once (115 KB in internal RAM) and
// caches three eye levels in PSRAM, so a blink is a blit.
//
// The classic ESP32 has NO PSRAM, and its largest contiguous DRAM block is
// only ~110-120 KB — about the size of one full frame, which WiFi and TLS
// then have to fit around. So it renders the SAME picture in horizontal
// bands: 240x48 is 23 KB, and each band is pushed as it is finished.
//
// kBandH == H on the S3 means a single band, i.e. exactly the old code path.
// That is deliberate: banding must not be able to regress the target that
// already works.
#if CONFIG_IDF_TARGET_ESP32S3
constexpr int kBandH = H;
constexpr bool kUseCache = true;
#else
constexpr int kBandH = 48;
constexpr bool kUseCache = false;  // no PSRAM to cache into
#endif
constexpr int kBands = (H + kBandH - 1) / kBandH;

LGFX_Buddy lcd;
LGFX_Sprite spr(&lcd);                 // the working frame (one band tall)
LGFX_Sprite cache[kLevelCount] = {LGFX_Sprite(&lcd), LGFX_Sprite(&lcd), LGFX_Sprite(&lcd)};
uint16_t* fb = nullptr;                // spr's buffer, biased so fb[y*W+x]
                                       // takes ABSOLUTE y inside the band
int band_y0 = 0;                       // first row of the band being drawn
int cached_emotion = -1;

inline bool in_band(int y) { return y >= band_y0 && y < band_y0 + kBandH; }

volatile int s_emotion = 0;
volatile bool s_dirty = true;

inline float clampf(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }

constexpr uint16_t rgb(int r, int g, int b) {
  return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

// LovyanGFX stores 16bpp sprites BIG-endian, because that is the order the SPI
// bus consumes. Writing native little-endian into getBuffer() renders flat
// colours as the wrong colour and gradients as horizontal rainbow stripes.
// Verified on hardware with a three-way test card. Convert at the boundary.
inline uint16_t to_store(uint16_t native) { return __builtin_bswap16(native); }
inline uint16_t from_store(uint16_t stored) { return __builtin_bswap16(stored); }

// Ordered dither. RGB565 throws away 3 bits of red/blue and 2 of green, which
// bands a smooth ramp badly — that is why the gradient was removed the first
// time. Nudging each pixel by a Bayer-patterned 1.5 quantisation steps before
// truncation turns the bands into a stipple the eye reads as smooth.
const uint8_t kBayer[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};
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
// The buffer only holds the current band, so absolute y is shifted down to it.
inline uint16_t* row_ptr(int y) { return &fb[(y - band_y0) * W]; }

inline void blend_at(int x, int y, uint16_t c, float a) {
  if (x < 0 || x >= W || !in_band(y) || a <= 0.f) return;
  uint16_t& p = row_ptr(y)[x];
  p = to_store(blend565(from_store(p), c, a > 1.f ? 1.f : a));
}
// Same clip for opaque writes. Every path into the framebuffer goes through
// one of these two, which is what makes banding safe: a draw routine keeps
// using absolute screen coordinates and simply produces nothing off-band.
inline void put_at(int x, int y, uint16_t stored) {
  if (x < 0 || x >= W || !in_band(y)) return;
  row_ptr(y)[x] = stored;
}

// Paint one whole screen. Runs `paint` once per band and pushes each band as
// it is finished; on the S3 kBands is 1, so this is the original single-shot
// path with no behavioural change.
template <typename F>
void render_bands(F&& paint) {
  for (int b = 0; b < kBands; b++) {
    band_y0 = b * kBandH;
    spr.fillScreen(TFT_BLACK);
    paint();
    spr.pushSprite(0, band_y0);
  }
  band_y0 = 0;
}

// ===== the eye =====
void draw_eye(int cxi, int side, const Emotion& em, int open_pct, int gx, int gy) {
  const EyeStyle& e = em.eye;
  const float eyeW = e.width * S;
  const float full = e.height * S * (e.openness / 100.f);  // fully-open height
  float open = full * (open_pct / 100.f);
  if (open < 6.f) open = 6.f;
  // A blink closes DOWNWARD — the lower lid stays put and the upper lid comes
  // down to meet it. Shrinking about the centre reads as a squash, not a blink.
  const float cx = cxi + gx;
  const float cy = (CY + gy + full / 2) - open / 2;
  const float hw = eyeW / 2, hh = open / 2;
  float r = eyeW * 0.42f;
  if (r > hh) r = hh;
  const float browAmt = e.brow != 0 ? open * kBrowDepth : 0.f;
  const float top = cy - hh;
  // The squint must shrink with the eye. As an absolute offset it sat above
  // the shrunken eye mid-blink and removed all of it — happy did not blink,
  // it vanished and reappeared.
  const float lift_px = e.lift * S * (open / full);

  const int gm = 10;  // glow margin
  const int x0 = static_cast<int>(cx - hw) - gm, x1 = static_cast<int>(cx + hw) + gm;
  // Clip the SCAN to the band, not just the writes. Letting the loop run the
  // whole eye and discarding out-of-band pixels in put_at/blend_at means the
  // per-pixel SDF maths runs once per band — measured at 251 ms/frame on a
  // classic ESP32 (4 fps) before this line existed.
  int y0 = static_cast<int>(top) - gm, y1 = static_cast<int>(cy + hh) + gm;
  if (y0 < band_y0) y0 = band_y0;
  if (y1 > band_y0 + kBandH - 1) y1 = band_y0 + kBandH - 1;
  const float span = open < 1.f ? 1.f : open;
  const uint16_t glow_col = rgb(em.r / 4, em.g / 4, em.b / 4);
  const float ihw = hw - r, ihh = hh - r;

  for (int y = y0; y <= y1; y++) {
    const float qy = fabsf(y + 0.5f - cy) - ihh;
    const float ay = qy > 0.f ? qy : 0.f;
    const float ay2 = ay * ay;
    for (int x = x0; x <= x1; x++) {
      // sqrt is only needed at the four rounded corners, where qx and qy are
      // both positive; everywhere else it was computing sqrtf(0).
      const float qx = fabsf(x + 0.5f - cx) - ihw;
      float outside;
      if (qx > 0.f) outside = (ay > 0.f) ? sqrtf(qx * qx + ay2) : qx;
      else outside = ay;
      const float d = outside + fminf(fmaxf(qx, qy), 0.f) - r;
      if (d > 7.f) continue;

      // The lid cut applies to the GLOW as well as the eye body — a lid that
      // covers the eye covers its halo. Cutting only the body left happy with
      // a dark block where the squint should have shown black.
      float cut = 1.f;
      if (browAmt > 0.f) {
        const float t = clampf((x + 0.5f - (cx - hw)) / eyeW, 0.f, 1.f);
        const bool deep_right = (e.brow > 0) == (side == 0);
        const float frac = deep_right ? t : 1.f - t;
        cut *= clampf((y + 0.5f) - (top + browAmt * frac) + 0.5f, 0.f, 1.f);
      }
      if (e.lift > 0)
        cut *= clampf((cy + hh - lift_px) - (y + 0.5f) + 0.5f, 0.f, 1.f);
      if (cut <= 0.f) continue;

      const float ecov = clampf(0.5f - d, 0.f, 1.f) * cut;
      const float gcov = clampf((7.f - d) / 7.f, 0.f, 1.f) * 0.5f * cut;

      uint16_t col = 0;
      if (ecov > 0.f) {
        const float t = clampf(((y + 0.5f) - top) / span, 0.f, 1.f);
        const float k = 1.0f - 0.55f * t;  // never above 1.0: it would clip
        col = rgb_dither(x, y, em.r * k, em.g * k, em.b * k);
      }
      if (ecov >= 0.999f) {
        put_at(x, y, to_store(col));
      } else {
        if (gcov > 0.f) blend_at(x, y, glow_col, gcov);
        if (ecov > 0.f) blend_at(x, y, col, ecov);
      }
    }
  }
}

void build_cache(int emo) {
  // Only reachable when kUseCache, which is also the only case where spr is
  // full height — the memcpy below copies W*H, so make that dependency a
  // compile error rather than a corruption if the band config ever changes.
  static_assert(!kUseCache || kBandH == H, "cache requires a full-height frame");
  if (cached_emotion == emo) return;
  const int64_t t0 = esp_timer_get_time();
  for (int i = 0; i < kLevelCount; i++) {
    spr.fillScreen(TFT_BLACK);
    draw_eye(CX - GAP, 0, emotions()[emo], kLevels[i], 0, 0);
    draw_eye(CX + GAP, 1, emotions()[emo], kLevels[i], 0, 0);
    memcpy(cache[i].getBuffer(), spr.getBuffer(), static_cast<size_t>(W) * H * 2);
  }
  cached_emotion = emo;
  ESP_LOGD(TAG, "cache rebuilt for %s in %.0f ms", emotions()[emo].name.c_str(),
           (esp_timer_get_time() - t0) / 1000.0);
}

// How long a full eye frame costs, averaged over a window. Cheap enough to
// leave in (two timer reads per frame) and it is the first number anyone
// bringing up a new board wants: the cached and banded paths differ by an
// order of magnitude, so "is this board slow?" has an answer in the log.
#if CONFIG_BUDDY_DEBUG
void report_frame(int64_t us) {
  static int64_t sum = 0;
  static int n = 0;
  sum += us;
  if (++n < 40) return;
  const float ms = sum / 1000.0f / n;
  ESP_LOGI(TAG, "render %.1f ms/frame (%.1f fps) · %d band%s, cache %s",
           ms, 1000.0f / ms, kBands, kBands == 1 ? "" : "s",
           kUseCache ? "on" : "off");
  sum = 0;
  n = 0;
}
#endif

void draw_eyes_inner(int open_pct, int gx, int gy) {
  if (kUseCache) {
    // build_cache uses spr as scratch, so it must run BEFORE the clear —
    // otherwise its last level survives under the transparent blit and shows
    // as a stray bar below the eyes.
    build_cache(s_emotion);
    int li = 0;
    for (int i = 1; i < kLevelCount; i++)
      if (abs(kLevels[i] - open_pct) < abs(kLevels[li] - open_pct)) li = i;
    spr.fillScreen(TFT_BLACK);
    cache[li].pushSprite(&spr, gx, gy, TFT_BLACK);  // black = transparent
    spr.pushSprite(0, 0);
    return;
  }
  // No PSRAM to cache into: draw the eyes for real, band by band. Slower —
  // this is the pre-cache path, ~13 fps — but pixel-identical output.
  render_bands([&] {
    draw_eye(CX - GAP, 0, emotions()[s_emotion], open_pct, gx, gy);
    draw_eye(CX + GAP, 1, emotions()[s_emotion], open_pct, gx, gy);
  });
}

void draw_eyes(int open_pct, int gx, int gy) {
#if CONFIG_BUDDY_DEBUG
  const int64_t t0 = esp_timer_get_time();
  draw_eyes_inner(open_pct, gx, gy);
  report_frame(esp_timer_get_time() - t0);
#else
  draw_eyes_inner(open_pct, gx, gy);
#endif
}

// ===== speech =====
// LovyanGFX fonts, replacing the hand-rolled 5x7 bitmap font. That font also
// shipped a buffer overrun that rebooted the device on long replies; letting
// the library measure and draw removes the whole class of bug.
void draw_text(const char* text) {
  // Wrapping is measured once, outside the band loop — it only needs the font
  // metrics, not the pixels.
  spr.setFont(&FontLatin);
  const Emotion& em = emotions()[s_emotion];
  spr.setTextColor(spr.color565(em.r, em.g, em.b));
  spr.setTextDatum(middle_center);

  constexpr int kMaxW = 196;  // chord of the round panel, with a margin
  constexpr int kMaxLines = 6;
  constexpr int kMaxCol = 48;
  char lines[kMaxLines][kMaxCol + 1] = {};
  int nlines = 0, col = 0;

  // Greedy wrap, measured with the real font. Widths are added rather than
  // formatted into a scratch buffer, so there is no length to get wrong —
  // the previous hand-rolled version of this shipped a buffer overrun that
  // rebooted the device on long replies.
  const int space_w = spr.textWidth(" ");
  const char* p = text;
  while (*p && nlines < kMaxLines) {
    while (*p == ' ') p++;
    const char* start = p;
    while (*p && *p != ' ') p++;
    int len = static_cast<int>(p - start);
    if (len == 0) break;
    if (len > kMaxCol) len = kMaxCol;
    char word[kMaxCol + 1];
    memcpy(word, start, len);
    word[len] = '\0';

    const int word_w = spr.textWidth(word);
    if (col > 0 && spr.textWidth(lines[nlines]) + space_w + word_w > kMaxW) {
      if (++nlines >= kMaxLines) break;
      col = 0;
    }
    if (col > 0 && col + 1 + len <= kMaxCol) {
      lines[nlines][col++] = ' ';
      memcpy(lines[nlines] + col, word, len + 1);
      col += len;
    } else if (col == 0) {
      memcpy(lines[nlines], word, len + 1);
      col = len;
    }
  }
  const int used = (col > 0) ? nlines + 1 : nlines;
  const int pitch = spr.fontHeight() + 2;
  const int y_top = CY - (used - 1) * pitch / 2;
  render_bands([&] {
    spr.setFont(&FontLatin);
    spr.setTextColor(spr.color565(em.r, em.g, em.b));
    spr.setTextDatum(middle_center);
    int y = y_top;
    for (int i = 0; i < used; i++) {
      spr.drawString(lines[i], CX, y - band_y0);  // sprite-local y
      y += pitch;
    }
  });
}

// ===== boot splash =====
// The buddy is not idle during boot: wifi_start can block for 15 s. Showing
// the logo plus what it is actually doing turns that wait into a diagnosis.
char s_status[32] = "starting";
volatile bool s_boot_ready = false;
std::mutex s_status_mu;

void draw_logo(int ox, int oy) {
  // Same band clip as draw_eye: skip whole rows rather than testing every
  // pixel of them.
  int y0 = 0, y1 = kLogoH - 1;
  if (oy + y0 < band_y0) y0 = band_y0 - oy;
  if (oy + y1 > band_y0 + kBandH - 1) y1 = band_y0 + kBandH - 1 - oy;
  for (int y = y0; y <= y1; y++)
    for (int x = 0; x < kLogoW; x++) {
      const uint8_t a = kLogoA[y * kLogoW + x];
      if (a) blend_at(ox + x, oy + y, kLogoRGB[y * kLogoW + x], a / 255.f);
    }
}

inline uint16_t scale_rgb(uint16_t c, float k) {
  int r = ((c >> 11) & 0x1F), g = ((c >> 5) & 0x3F), b = (c & 0x1F);
  r = static_cast<int>(r * k);
  g = static_cast<int>(g * k);
  b = static_cast<int>(b * k);
  if (r > 31) r = 31;
  if (g > 63) g = 63;
  if (b > 31) b = 31;
  return static_cast<uint16_t>((r << 11) | (g << 5) | b);
}

// CRT / VHS damage, applied to the finished frame. amt 1 is barely a signal,
// 0 is clean. Effects in the order a real bad signal applies them.
// Every effect below is ROW-LOCAL — none of them reads a row other than the
// one being written — which is the property that lets the glitch survive being
// rendered in bands. The one thing that would NOT survive is esp_random():
// each band would draw different damage and the seams would show. So the
// caller passes a per-frame seed and this replays the identical sequence in
// every band, clipping to whichever rows are in front of it.
// xorshift32, not an LCG: the snow reads bits 20-24 for brightness and 0-8 for
// position, and an LCG's low bits are too regular for that (and a shifted-down
// LCG loses the high bits entirely, which silently turned every fleck grey).
// Seeds are forced odd by the caller, so the state can never reach 0.
uint32_t g_glitch_seed = 1;
inline uint32_t grnd() {
  g_glitch_seed ^= g_glitch_seed << 13;
  g_glitch_seed ^= g_glitch_seed >> 17;
  g_glitch_seed ^= g_glitch_seed << 5;
  return g_glitch_seed;
}

void glitch_frame(float amt, int roll, uint32_t seed) {
  if (amt <= 0.f) return;
  static uint16_t row[W];
  g_glitch_seed = seed;

  const float dim = 1.0f - 0.28f * amt;                 // scanlines
  for (int y = 1; y < H; y += 2) {
    if (!in_band(y)) continue;
    uint16_t* r = row_ptr(y);
    for (int x = 0; x < W; x++) r[x] = to_store(scale_rgb(from_store(r[x]), dim));
  }

  const int bands = 2 + grnd() % 5 + static_cast<int>(amt * 4);
  for (int i = 0; i < bands; i++) {                     // sync loss: band tearing
    const int y0 = grnd() % H, hgt = 2 + grnd() % 14;
    const int dx = static_cast<int>((static_cast<int>(grnd() % 41) - 20) * amt);
    if (!dx) continue;
    for (int y = y0; y < y0 + hgt && y < H; y++) {
      if (!in_band(y)) continue;
      uint16_t* r = row_ptr(y);
      memcpy(row, r, sizeof row);
      for (int x = 0; x < W; x++) {
        const int sx = x - dx;
        r[x] = (sx >= 0 && sx < W) ? row[sx] : 0;
      }
    }
  }

  for (int i = 0; i < 2; i++) {                         // colour carrier mistrack
    const int y0 = grnd() % H, hgt = 6 + grnd() % 26;
    const int dx = 1 + static_cast<int>(amt * 7);
    for (int y = y0; y < y0 + hgt && y < H; y++) {
      if (!in_band(y)) continue;
      uint16_t* r = row_ptr(y);
      memcpy(row, r, sizeof row);
      for (int x = 0; x < W; x++) {
        const int xr = x - dx < 0 ? 0 : x - dx;
        const int xb = x + dx >= W ? W - 1 : x + dx;
        r[x] = to_store((from_store(row[xr]) & 0xF800) |
                        (from_store(row[x]) & 0x07E0) |
                        (from_store(row[xb]) & 0x001F));
      }
    }
  }

  for (int y = roll; y < roll + 22 && y < H; y++) {     // vertical hold slipping
    if (y < 0 || !in_band(y)) continue;
    uint16_t* r = row_ptr(y);
    for (int x = 0; x < W; x++)
      r[x] = to_store(scale_rgb(from_store(r[x]), 1.0f + 0.9f * amt));
  }

  const int flecks = static_cast<int>(amt * 900);       // snow
  for (int i = 0; i < flecks; i++) {
    const uint32_t r = grnd();
    const uint8_t v = (r >> 20) & 0x1F;
    put_at(r % W, (r >> 9) % H,
           to_store(v > 20 ? 0xFFFF : rgb(v * 6, v * 6, v * 6)));
  }
}

void draw_splash(float amt, int roll, bool with_logo) {
  const uint32_t seed = esp_random() | 1u;  // one draw of the damage per frame
  render_bands([&] {
    if (with_logo) draw_logo((W - kLogoW) / 2, (H - kLogoH) / 2 - 12);
    {
      std::lock_guard<std::mutex> lock(s_status_mu);
      spr.setFont(&FontLatin);
      spr.setTextDatum(top_center);
      spr.setTextColor(spr.color565(120, 130, 145));
      spr.drawString(s_status, CX, 200 - band_y0);  // sprite-local y
    }
    glitch_frame(amt, roll, seed);
  });
}

void splash_boot() {
  // Tune in: heavy damage decaying to a clean picture.
  const int64_t t0 = esp_timer_get_time();
  int roll = -30;
  for (;;) {
    const float t = (esp_timer_get_time() - t0) / 1600000.f;
    if (t >= 1.f) break;
    draw_splash(t < 0.22f ? 1.0f : clampf((1.f - t) / 0.78f, 0.f, 1.f), roll, t > 0.12f);
    roll += 11;
    if (roll > H) roll = -30;
  }
  // Then hold, updating the status line, until the rest of the system says go.
  while (!s_boot_ready) {
    draw_splash(0.f, 0, true);
    vTaskDelay(pdMS_TO_TICKS(120));
  }
  // Glitch back out into the face, so the visual language is consistent.
  const int64_t t1 = esp_timer_get_time();
  while (esp_timer_get_time() - t1 < 420000) {
    const float t = (esp_timer_get_time() - t1) / 420000.f;
    const uint32_t seed = esp_random() | 1u;
    render_bands([&] {
      if (t < 0.5f) draw_logo((W - kLogoW) / 2, (H - kLogoH) / 2 - 12);
      else draw_eye(CX - GAP, 0, emotions()[s_emotion], 100, 0, 0),
           draw_eye(CX + GAP, 1, emotions()[s_emotion], 100, 0, 0);
      glitch_frame(0.85f, static_cast<int>(t * H), seed);
    });
  }
}

// ===== speech / gaze state =====
std::mutex s_say_mu;
char s_say_text[128];
volatile int64_t s_say_until = 0;
volatile bool s_say_dirty = false;


// face.look sets a target; while active the eyes follow it instead of doing
// idle saccades. Any Sense or reflex can drive it. The target expires so the
// buddy always drifts back to being itself.
volatile int s_look_tx = 0, s_look_ty = 0;
volatile int64_t s_look_until = 0;

void face_task(void*) {
  splash_boot();
  s_dirty = true;

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
    if (looking) {
      const int nx = gaze_x + (s_look_tx - gaze_x) / 2;
      const int ny = gaze_y + (s_look_ty - gaze_y) / 2;
      if (nx != gaze_x || ny != gaze_y || s_dirty) {
        gaze_x = nx; gaze_y = ny; s_dirty = false;
        draw_eyes(100, gaze_x, gaze_y);
      }
      if (now >= next_blink) {
        for (int li : {1, 2, 1}) draw_eyes(kLevels[li], gaze_x, gaze_y);
        draw_eyes(100, gaze_x, gaze_y);
        const int period = emotions()[s_emotion].blink_period_ms;
        next_blink = now + period / 2 + esp_random() % period;
      }
      vTaskDelay(pdMS_TO_TICKS(30));
      continue;
    }

    if (s_dirty) { s_dirty = false; draw_eyes(100, gaze_x, gaze_y); }
    if (now >= next_blink) {
      for (int li : {1, 2, 1}) draw_eyes(kLevels[li], gaze_x, gaze_y);
      draw_eyes(100, gaze_x, gaze_y);
      const int period = emotions()[s_emotion].blink_period_ms;
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

}  // namespace

void face_start() {
  lcd.setBrightness(0);  // dark until there is something to show
  lcd.init();

  spr.setColorDepth(16);
  spr.setPsram(false);  // internal RAM is ~2x faster to draw into, and it fits
  if (!spr.createSprite(W, kBandH)) {
    spr.setPsram(true);
    if (!spr.createSprite(W, kBandH)) { ESP_LOGE(TAG, "no memory for the frame"); return; }
  }
  fb = static_cast<uint16_t*>(spr.getBuffer());
  ESP_LOGI(TAG, "frame %dx%d in %d band%s (%u B)%s", W, kBandH, kBands,
           kBands == 1 ? "" : "s",
           static_cast<unsigned>(W) * kBandH * 2,
           kUseCache ? "" : ", no cache (no PSRAM)");
  if (kUseCache) {
    for (int i = 0; i < kLevelCount; i++) {
      cache[i].setColorDepth(16);
      cache[i].setPsram(true);  // 3 x 115 KB, only ever blitted
      if (!cache[i].createSprite(W, H)) ESP_LOGE(TAG, "no memory for eye cache %d", i);
    }
  }

  bus().subscribe("face.emotion", [](const Event& ev) {
    const int i = emotion_index(ev.payload.c_str());
    if (i >= 0) { s_emotion = i; s_dirty = true; }
    else ESP_LOGW(TAG, "unknown emotion '%s'", ev.payload.c_str());
  });
  bus().subscribe("face.say", [](const Event& ev) {
    std::lock_guard<std::mutex> lock(s_say_mu);
    latin1::copy_display_text(s_say_text, sizeof s_say_text, ev.payload.c_str());
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
    s_look_tx = x * 30 / 100;
    s_look_ty = y * 22 / 100;
    s_look_until = esp_log_timestamp() + 1500;
  });
  // The boot splash's status line is driven by whatever app_main is actually
  // doing — a real step, not a timer.
  bus().subscribe("boot.status", [](const Event& ev) {
    std::lock_guard<std::mutex> lock(s_status_mu);
    strncpy(s_status, ev.payload.c_str(), sizeof s_status - 1);
    s_status[sizeof s_status - 1] = '\0';
  });
  bus().subscribe("boot.ready", [](const Event&) { s_boot_ready = true; });

  // First frame is pure noise, so the backlight can come straight up: the
  // static IS the fade-in, and the uninitialised panel is never seen.
  render_bands([seed = esp_random() | 1u] { glitch_frame(1.f, 40, seed); });
  lcd.setBrightness(160);

  xTaskCreatePinnedToCore(face_task, "face", 6144, nullptr, 4, nullptr, 1);
}

}  // namespace buddy
