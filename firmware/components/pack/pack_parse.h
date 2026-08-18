#pragma once
// JSON -> expression/mood tables. Header-only and free of ESP dependencies so
// it can be exercised on the host under sanitizers, which is where the NDEF
// decoder's out-of-bounds read was caught.
//
// Everything here treats the pack as UNTRUSTED. Packs get shared, mailed and
// tapped in from stickers, so a hostile or merely sloppy one must produce a
// dull face, never a crash and never a renderer fed nonsense. The rules:
//   - a missing field takes the default
//   - a field of the wrong type is ignored, not coerced
//   - every number is clamped to a range the renderer can survive
//   - collections are capped
#include "cJSON.h"

#include <cmath>
#include <cstring>
#include <string>
#include <vector>

#include "face_model.h"
#include "mood_model.h"

namespace buddy {
namespace packparse {

constexpr size_t kMaxEntries = 64;  // expressions or moods in one pack
constexpr size_t kMaxColors = 8;    // colour stops in one mood

inline int clamp_int(double v, int lo, int hi) {
  if (!std::isfinite(v)) return lo;
  if (v < lo) return lo;
  if (v > hi) return hi;
  return static_cast<int>(v);
}

inline float clamp_float(double v, float lo, float hi) {
  if (!std::isfinite(v)) return lo;
  if (v < lo) return lo;
  if (v > hi) return hi;
  return static_cast<float>(v);
}

// Reads an integer member, clamped. Absent or non-numeric leaves `out` alone.
inline void get_int(const cJSON* o, const char* key, int& out, int lo, int hi) {
  const cJSON* it = cJSON_GetObjectItemCaseSensitive(o, key);
  if (cJSON_IsNumber(it)) out = clamp_int(it->valuedouble, lo, hi);
}

inline void get_float(const cJSON* o, const char* key, float& out, float lo, float hi) {
  const cJSON* it = cJSON_GetObjectItemCaseSensitive(o, key);
  if (cJSON_IsNumber(it)) out = clamp_float(it->valuedouble, lo, hi);
}

inline void get_string(const cJSON* o, const char* key, std::string& out) {
  const cJSON* it = cJSON_GetObjectItemCaseSensitive(o, key);
  if (cJSON_IsString(it) && it->valuestring) out = it->valuestring;
}

// "#rrggbb" or "rrggbb". Anything else leaves `out` untouched and returns
// false — a typo in one colour must not take the whole mood down with it.
inline bool parse_hex_color(const char* s, Rgb& out) {
  if (!s) return false;
  if (*s == '#') s++;
  if (std::strlen(s) != 6) return false;
  uint32_t v = 0;
  for (int i = 0; i < 6; i++) {
    const char c = s[i];
    int d;
    if (c >= '0' && c <= '9') d = c - '0';
    else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
    else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
    else return false;
    v = (v << 4) | static_cast<uint32_t>(d);
  }
  out.r = static_cast<uint8_t>((v >> 16) & 0xFF);
  out.g = static_cast<uint8_t>((v >> 8) & 0xFF);
  out.b = static_cast<uint8_t>(v & 0xFF);
  return true;
}

inline Anim parse_anim(const std::string& s) {
  if (s == "solid") return Anim::Solid;
  if (s == "spin") return Anim::Spin;
  if (s == "pulse") return Anim::Pulse;
  if (s == "off") return Anim::Off;
  return Anim::Breathe;  // the safe default: something visible, nothing fast
}

// { "<name>": { "eye": {...}, "blink_ms": N, "color": "#rrggbb",
//               "mood": "...", "registro": "..." }, ... }
inline bool parse_expressions(const char* json, std::vector<Emotion>& out) {
  out.clear();
  cJSON* root = cJSON_Parse(json);
  if (!root) return false;
  if (!cJSON_IsObject(root)) { cJSON_Delete(root); return false; }

  for (const cJSON* it = root->child; it; it = it->next) {
    if (out.size() >= kMaxEntries) break;
    if (!it->string || !cJSON_IsObject(it)) continue;

    Emotion e;
    e.name = it->string;
    if (e.name.empty()) continue;

    const cJSON* eye = cJSON_GetObjectItemCaseSensitive(it, "eye");
    if (cJSON_IsObject(eye)) {
      // Bounds are the renderer's, not the format's: the panel is 240 px and
      // openness is a percentage. A pack asking for a 9000 px eye gets 120.
      get_int(eye, "width", e.eye.width, 1, 120);
      get_int(eye, "height", e.eye.height, 1, 120);
      get_int(eye, "openness", e.eye.openness, 0, 100);
      get_int(eye, "lift", e.eye.lift, 0, 60);
      get_int(eye, "brow", e.eye.brow, -1, 1);
    }
    get_int(it, "blink_ms", e.blink_period_ms, 200, 60000);
    get_string(it, "mood", e.mood);
    get_string(it, "registro", e.registro);

    const cJSON* col = cJSON_GetObjectItemCaseSensitive(it, "color");
    if (cJSON_IsString(col)) {
      Rgb c;
      if (parse_hex_color(col->valuestring, c)) { e.r = c.r; e.g = c.g; e.b = c.b; }
    }
    out.push_back(std::move(e));
  }
  cJSON_Delete(root);
  return !out.empty();
}

// { "<name>": { "anim": "...", "period_ms": N, "floor": F,
//               "dir": "cw"|"ccw", "colors": ["#rrggbb", ...] }, ... }
inline bool parse_moods(const char* json, std::vector<Mood>& out) {
  out.clear();
  cJSON* root = cJSON_Parse(json);
  if (!root) return false;

  const cJSON* obj = cJSON_IsObject(root)
                         ? cJSON_GetObjectItemCaseSensitive(root, "moods")
                         : nullptr;
  if (!cJSON_IsObject(obj)) obj = root;  // also accept a bare mood map
  if (!cJSON_IsObject(obj)) { cJSON_Delete(root); return false; }

  for (const cJSON* it = obj->child; it; it = it->next) {
    if (out.size() >= kMaxEntries) break;
    if (!it->string || !cJSON_IsObject(it)) continue;

    Mood m;
    m.name = it->string;
    if (m.name.empty()) continue;

    std::string anim;
    get_string(it, "anim", anim);
    m.anim = parse_anim(anim);

    // 20 ms is one ring frame; below that the animation is not a period, it is
    // a strobe. Ten minutes is the other end of anything meaningful.
    get_int(it, "period_ms", m.period_ms, 20, 600000);
    get_float(it, "floor", m.floor, 0.0f, 1.0f);

    std::string dir;
    get_string(it, "dir", dir);
    if (dir == "ccw") m.dir = -1;

    const cJSON* colors = cJSON_GetObjectItemCaseSensitive(it, "colors");
    if (cJSON_IsArray(colors)) {
      for (const cJSON* c = colors->child; c; c = c->next) {
        if (m.colors.size() >= kMaxColors) break;
        Rgb rgb;
        if (cJSON_IsString(c) && parse_hex_color(c->valuestring, rgb))
          m.colors.push_back(rgb);
      }
    }
    out.push_back(std::move(m));
  }
  cJSON_Delete(root);
  return !out.empty();
}

}  // namespace packparse
}  // namespace buddy
