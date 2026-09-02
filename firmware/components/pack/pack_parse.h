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
//   - collections are capped, and so is every string
//   - the document's nesting depth is checked BEFORE it is parsed
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

// A name becomes a filename (lines/<expression>.txt) and a log argument, so it
// is capped and screened, not just non-empty. 48 bytes is generous next to a
// 240 px screen. Over-long is REJECTED rather than truncated: cutting at a
// byte would split a UTF-8 sequence, and accented names are the point.
constexpr size_t kMaxName = 48;

// cJSON_Parse is recursive and costs ~96 bytes of stack per level, measured
// against the vendored copy. The main task gets 8192 bytes, so a document
// around 85 levels deep panics inside the parser -- before face_start(), with
// a pack that persists across reboots. That is a permanent boot loop from a
// 200-byte file, which is exactly the brick a pack must not be able to cause.
// Its own CJSON_NESTING_LIMIT of 1000 is two orders of magnitude too generous
// for this stack, so the depth is counted here first.
constexpr int kMaxDepth = 20;

// Counts nesting without allocating or recursing. Strings are skipped so that
// a bracket inside a string literal cannot inflate the count.
inline bool depth_ok(const char* json, int max_depth = kMaxDepth) {
  if (!json) return false;
  int depth = 0;
  bool in_string = false;
  for (const char* p = json; *p; p++) {
    if (in_string) {
      if (*p == '\\' && p[1]) p++;       // skip the escaped byte, whatever it is
      else if (*p == '"') in_string = false;
      continue;
    }
    if (*p == '"') in_string = true;
    else if (*p == '[' || *p == '{') { if (++depth > max_depth) return false; }
    else if (*p == ']' || *p == '}') { if (depth > 0) depth--; }
  }
  return true;
}

// Rejected outright rather than sanitised: a name is a filename, and quietly
// rewriting one would let two expressions collide on one bank.
inline bool name_ok(const std::string& n) {
  if (n.empty() || n.size() > kMaxName) return false;
  for (const char c : n) {
    if (c == '/' || c == '\\') return false;
    if (static_cast<unsigned char>(c) < 0x20) return false;   // controls, NUL
  }
  return n != "." && n != ".." && n.find("..") == std::string::npos;
}

// 1e999 is valid JSON syntax and cJSON stores it as +inf with type Number, so
// this runs on real input. Collapsing a non-finite to `lo` regardless of sign
// INVERTS the clamp: "impossibly large" used to come out as the smallest legal
// value, which for period_ms is the strobe the range exists to forbid. NaN has
// no side, so it takes the default end.
inline int clamp_int(double v, int lo, int hi) {
  if (std::isnan(v)) return lo;
  if (v < lo) return lo;
  if (v > hi) return hi;      // catches +inf
  return static_cast<int>(v);
}

inline float clamp_float(double v, float lo, float hi) {
  if (std::isnan(v)) return lo;
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

// Capped: `mood` reaches a log format string and would otherwise let a pack
// print 60 KB from the ring task.
inline void get_string(const cJSON* o, const char* key, std::string& out,
                       size_t max_len = kMaxName) {
  const cJSON* it = cJSON_GetObjectItemCaseSensitive(o, key);
  if (!cJSON_IsString(it) || !it->valuestring) return;
  const std::string v = it->valuestring;
  if (v.size() <= max_len) out = v;
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
//               "mood": "..." }, ... }
inline bool parse_expressions(const char* json, std::vector<Emotion>& out) {
  out.clear();
  if (!depth_ok(json)) return false;
  cJSON* root = cJSON_Parse(json);
  if (!root) return false;
  if (!cJSON_IsObject(root)) { cJSON_Delete(root); return false; }

  for (const cJSON* it = root->child; it; it = it->next) {
    if (out.size() >= kMaxEntries) break;
    if (!it->string || !cJSON_IsObject(it)) continue;

    Emotion e;
    e.name = it->string;
    if (!name_ok(e.name)) continue;

    const cJSON* eye = cJSON_GetObjectItemCaseSensitive(it, "eye");
    if (cJSON_IsObject(eye)) {
      // Bounds are the renderer's, not the format's: the panel is 240 px and
      // openness is a percentage. A pack asking for a 9000 px eye gets 120.
      get_int(eye, "width", e.eye.width, 1, 120);
      get_int(eye, "height", e.eye.height, 1, 120);
      // Floor of 1, not 0: the renderer divides by height * openness to place
      // the lift, so a zero here is a division by zero that turns into an
      // infinite offset and blanks BOTH eyes -- a fully black panel from a
      // field that was individually inside its range.
      get_int(eye, "openness", e.eye.openness, 1, 100);
      get_int(eye, "lift", e.eye.lift, 0, 60);
      get_int(eye, "brow", e.eye.brow, -1, 1);
    }
    get_int(it, "blink_ms", e.blink_period_ms, 200, 60000);
    get_string(it, "mood", e.mood);

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
// `bare_map` says whether a document with no "moods" member may be read as one
// -- true for a standalone moods file, false for the manifest. The caller
// knows which file it opened; the parser cannot tell, and guessing was a real
// hazard: applied to the manifest, `{"id":"zero","expressions":{...}}` turned
// every top-level key into a mood and replaced the four built-ins with one
// called "expressions". A single typo -- "mood" for "moods" -- did it.
inline bool parse_moods(const char* json, std::vector<Mood>& out,
                        bool bare_map = false) {
  out.clear();
  if (!depth_ok(json)) return false;
  cJSON* root = cJSON_Parse(json);
  if (!root) return false;

  const cJSON* obj = cJSON_IsObject(root)
                         ? cJSON_GetObjectItemCaseSensitive(root, "moods")
                         : nullptr;
  if (!cJSON_IsObject(obj) && bare_map) obj = root;
  if (!cJSON_IsObject(obj)) { cJSON_Delete(root); return false; }

  for (const cJSON* it = obj->child; it; it = it->next) {
    if (out.size() >= kMaxEntries) break;
    if (!it->string || !cJSON_IsObject(it)) continue;

    Mood m;
    m.name = it->string;
    if (!name_ok(m.name)) continue;

    std::string anim;
    get_string(it, "anim", anim);
    m.anim = parse_anim(anim);

    // Two ring frames is the floor, and it has to be: at one frame the phase
    // advances by exactly 1.0 and floor() puts it straight back to zero, so
    // the ring FREEZES instead of strobing. kMinPeriodMs is derived from the
    // ring's frame time in mood_model.h so the two cannot drift apart.
    get_int(it, "period_ms", m.period_ms, kMinPeriodMs, 600000);
    get_float(it, "floor", m.floor, 0.0f, 1.0f);

    // Only the two documented values mean anything; anything else keeps the
    // default AND says so, because a typo here renders a wrong direction with
    // no other symptom.
    std::string dir;
    get_string(it, "dir", dir);
    if (dir == "ccw") m.dir = -1;
    else if (!dir.empty() && dir != "cw") m.unknown_dir = true;

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
