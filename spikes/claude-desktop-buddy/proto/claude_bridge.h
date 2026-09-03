#pragma once
// Decoder for the "Hardware Buddy" BLE bridge that Claude for macOS/Windows
// speaks -- the protocol documented in anthropics/claude-desktop-buddy's
// REFERENCE.md -- mapped onto our event bus.
//
// Header-only and free of ESP dependencies, so the whole thing runs on the
// host under sanitizers. That is the point of the spike: every byte here
// arrives over the air from a peer we did not write, on a link that is only
// encrypted if we bond. Until the passkey pairing is in, "the desktop app" is
// whoever is in radio range with an nRF dongle. So this file treats the
// bridge exactly like the pack loader treats a pack, and for the same reason:
//   - a missing field takes the default
//   - a field of the wrong type is ignored, not coerced
//   - every number is clamped, every string capped on a UTF-8 boundary
//   - the document's nesting depth is checked BEFORE it is parsed
//   - an over-long line is discarded WHOLE, never truncated into a fragment
//
// What this deliberately is NOT: a Brain. The desktop does not answer
// questions -- it pushes telemetry and takes yes/no on permission prompts.
// So the bridge is a Sense (facts in) plus one narrow command channel
// (a decision out), and nothing here reaches brain.ask.

#include "cJSON.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace buddy {
namespace claudebridge {

// ---------------------------------------------------------------------------
// Caps
// ---------------------------------------------------------------------------

// The desktop drops turn events that serialize over 4 KB, so 4 KB plus slack
// is the largest legitimate line. Worth stating because the reference
// firmware's line buffer is 1024 bytes: every turn event between 1 KB and
// 4 KB silently vanishes there, split into two fragments that both fail to
// parse. Ours drops them on purpose or not at all.
constexpr size_t kMaxLine = 4352;

constexpr size_t kMaxMsg = 64;       // one-line summary, for the face
constexpr size_t kMaxEntry = 96;     // one transcript line
constexpr size_t kMaxEntries = 6;    // how many we keep; the desktop sends "a few"
constexpr size_t kMaxId = 64;        // prompt id -- opaque, we only echo it
constexpr size_t kMaxTool = 24;      // "Bash", "Edit", ...
constexpr size_t kMaxHint = 96;      // the command line being approved
constexpr size_t kMaxTurn = 192;     // assistant text; a round face holds little
constexpr size_t kMaxName = 40;      // folder-push file name

// cJSON_Parse recurses. The pack parser measured ~96 bytes of stack per level
// against the vendored copy and settled on 20; the bridge's own documents are
// three levels deep at most, so this can be tighter still.
constexpr int kMaxDepth = 12;

// REFERENCE.md: "If you don't receive a snapshot for ~30 seconds, treat the
// connection as dead."
constexpr uint32_t kSilenceMs = 30000;

// ---------------------------------------------------------------------------
// Text hygiene
// ---------------------------------------------------------------------------

// Copy at most `cap` BYTES of `src`, dropping control characters and stopping
// only on a UTF-8 sequence boundary. Both halves matter:
//   - control bytes reach a renderer and a log line, and 0x00-0x1F in a name
//     is how you get a stray newline into a hand-built JSON frame;
//   - cutting mid-sequence splits an accented character into two invalid
//     bytes, which is the exact bug class the latin1 normaliser's tests
//     exist for. An accented pet name is not an edge case here, it is Tuesday.
// Malformed input (a lone continuation byte, a truncated tail) loses the bad
// byte and keeps going -- the buddy shows what it can rather than going mute.
inline std::string sanitize(const char* src, size_t cap) {
  std::string out;
  if (!src) return out;
  out.reserve(cap < 64 ? cap : 64);
  const unsigned char* p = reinterpret_cast<const unsigned char*>(src);
  while (*p) {
    const unsigned char c = *p;
    if (c >= 0x80 && c < 0xC0) { p++; continue; }  // continuation with no lead

    size_t seq = 1;
    if (c >= 0xF0) seq = 4;
    else if (c >= 0xE0) seq = 3;
    else if (c >= 0xC0) seq = 2;

    bool ok = true;
    for (size_t i = 1; i < seq; i++) {
      // p[i] is at worst the NUL terminator, which is in bounds and fails
      // this test -- a truncated tail cannot read past the string.
      if ((p[i] & 0xC0) != 0x80) { ok = false; break; }
    }
    if (!ok) { p++; continue; }
    if (seq == 1 && (c < 0x20 || c == 0x7F)) { p++; continue; }
    if (out.size() + seq > cap) break;
    out.append(reinterpret_cast<const char*>(p), seq);
    p += seq;
  }
  return out;
}

// Stricter still: what may go inside a JSON string we build with snprintf.
// The other end of this bridge learned it the hard way -- a quote in a name
// persisted to NVS and broke its status frame until the name was re-set.
inline std::string json_safe(const std::string& s, size_t cap) {
  std::string out;
  out.reserve(s.size() < cap ? s.size() : cap);
  for (char ch : s) {
    const unsigned char c = static_cast<unsigned char>(ch);
    if (c < 0x20 || c == 0x7F || c == '"' || c == '\\') continue;
    if (out.size() >= cap) break;
    out.push_back(ch);
  }
  return out;
}

// Counts nesting without allocating or recursing. Strings are skipped so a
// bracket inside a string literal cannot inflate the count. (Copied from
// firmware/components/pack/pack_parse.h rather than included: a spike that
// makes firmware/ a build dependency is no longer disposable.)
inline bool depth_ok(const char* json, int max_depth = kMaxDepth) {
  if (!json) return false;
  int depth = 0;
  bool in_string = false;
  for (const char* p = json; *p; p++) {
    if (in_string) {
      if (*p == '\\' && p[1]) p++;
      else if (*p == '"') in_string = false;
      continue;
    }
    if (*p == '"') in_string = true;
    else if (*p == '[' || *p == '{') { if (++depth > max_depth) return false; }
    else if (*p == ']' || *p == '}') { if (depth > 0) depth--; }
  }
  return true;
}

// ---------------------------------------------------------------------------
// Line framing
// ---------------------------------------------------------------------------

// Newline-delimited JSON over a stream that fragments at the MTU boundary:
// accumulate until '\n', then hand the line over.
//
// The interesting case is the over-long line. Truncating it yields a shorter
// document that may still parse -- a hostile peer picks what survives the
// cut. Worse, resuming mid-line makes the NEXT frame start at a random
// offset, and the stream never re-syncs. So an overrun discards everything up
// to and including the next newline, and the frame after it parses normally.
class LineReader {
 public:
  // Calls on_line(const std::string&) for each complete, in-budget line.
  template <typename Fn>
  void feed(const uint8_t* data, size_t n, Fn on_line) {
    for (size_t i = 0; i < n; i++) {
      const char c = static_cast<char>(data[i]);
      if (c == '\n' || c == '\r') {
        if (!dropping_ && !buf_.empty()) on_line(buf_);
        buf_.clear();
        dropping_ = false;
        continue;
      }
      if (dropping_) continue;
      if (buf_.size() >= kMaxLine) {
        buf_.clear();
        dropping_ = true;
        dropped_++;
        continue;
      }
      buf_.push_back(c);
    }
  }

  // Lines abandoned for overrunning kMaxLine. Non-zero means either a peer
  // that is not the desktop app, or a cap that needs raising -- both worth
  // seeing, neither worth a crash.
  uint32_t dropped() const { return dropped_; }
  size_t buffered() const { return buf_.size(); }

 private:
  std::string buf_;
  bool dropping_ = false;
  uint32_t dropped_ = 0;
};

// ---------------------------------------------------------------------------
// Frames
// ---------------------------------------------------------------------------

struct Snapshot {
  int total = 0;
  int running = 0;
  int waiting = 0;
  uint32_t tokens = 0;
  uint32_t tokens_today = 0;
  std::string msg;
  std::vector<std::string> entries;
  bool has_prompt = false;
  std::string prompt_id;
  std::string prompt_tool;
  std::string prompt_hint;
};

struct Frame {
  enum Kind {
    kNone,      // not a frame we recognise
    kSnapshot,  // heartbeat: sessions, transcript, pending permission
    kTurn,      // one completed assistant turn
    kTime,      // clock sync, sent once on connect
    kCommand,   // {"cmd": ...} -- name, owner, status, folder push
  };

  Kind kind = kNone;
  Snapshot snap;

  std::string text;   // kTurn: the text blocks, joined and capped
  std::string role;   // kTurn

  int64_t epoch = 0;      // kTime
  int32_t tz_offset = 0;  // kTime

  std::string cmd;    // kCommand
  std::string name;   // kCommand: name / owner / char_begin / file path
  uint32_t size = 0;  // kCommand: file size, char_begin total
};

namespace detail {

inline int clamp_int(const cJSON* v, int lo, int hi, int dflt) {
  if (!cJSON_IsNumber(v)) return dflt;
  const double d = cJSON_GetNumberValue(v);
  if (!std::isfinite(d)) return dflt;
  if (d < lo) return lo;
  if (d > hi) return hi;
  return static_cast<int>(d);
}

inline uint32_t clamp_u32(const cJSON* v, uint32_t dflt) {
  if (!cJSON_IsNumber(v)) return dflt;
  const double d = cJSON_GetNumberValue(v);
  if (!std::isfinite(d) || d < 0) return dflt;
  if (d > 4294967295.0) return 4294967295u;
  return static_cast<uint32_t>(d);
}

inline std::string str_of(const cJSON* v, size_t cap) {
  if (!cJSON_IsString(v)) return std::string();
  return sanitize(v->valuestring, cap);
}

}  // namespace detail

// Decode one line. Returns false for anything we do not recognise, which
// includes every malformed document -- the caller's job is then to do
// nothing, not to guess.
inline bool decode(const std::string& line, Frame& out) {
  out = Frame();
  if (line.empty() || line.size() > kMaxLine) return false;
  if (line[0] != '{') return false;          // arrays and scalars are not ours
  if (!depth_ok(line.c_str())) return false;

  cJSON* doc = cJSON_ParseWithLength(line.c_str(), line.size());
  if (!doc) return false;
  if (!cJSON_IsObject(doc)) { cJSON_Delete(doc); return false; }

  // Order matches the reference firmware: commands first, then the clock,
  // then turn events, and a snapshot is what is left.
  const cJSON* cmd = cJSON_GetObjectItemCaseSensitive(doc, "cmd");
  if (cJSON_IsString(cmd)) {
    out.kind = Frame::kCommand;
    out.cmd = detail::str_of(cmd, kMaxTool);
    const cJSON* nm = cJSON_GetObjectItemCaseSensitive(doc, "name");
    const cJSON* path = cJSON_GetObjectItemCaseSensitive(doc, "path");
    // "name" on the name/owner/char_begin commands, "path" on file.
    out.name = cJSON_IsString(path) ? detail::str_of(path, kMaxName)
                                    : detail::str_of(nm, kMaxName);
    const cJSON* sz = cJSON_GetObjectItemCaseSensitive(doc, "size");
    const cJSON* total = cJSON_GetObjectItemCaseSensitive(doc, "total");
    out.size = cJSON_IsNumber(sz) ? detail::clamp_u32(sz, 0)
                                  : detail::clamp_u32(total, 0);
    cJSON_Delete(doc);
    return true;
  }

  const cJSON* t = cJSON_GetObjectItemCaseSensitive(doc, "time");
  if (cJSON_IsArray(t) && cJSON_GetArraySize(t) == 2) {
    const cJSON* e = cJSON_GetArrayItem(t, 0);
    const cJSON* z = cJSON_GetArrayItem(t, 1);
    if (cJSON_IsNumber(e) && cJSON_IsNumber(z)) {
      out.kind = Frame::kTime;
      out.epoch = static_cast<int64_t>(detail::clamp_u32(e, 0));
      // A timezone is at most +14:00/-12:00; anything else is not a timezone.
      out.tz_offset = detail::clamp_int(z, -50400, 50400, 0);
      cJSON_Delete(doc);
      return true;
    }
  }

  const cJSON* evt = cJSON_GetObjectItemCaseSensitive(doc, "evt");
  if (cJSON_IsString(evt) && std::strcmp(evt->valuestring, "turn") == 0) {
    out.kind = Frame::kTurn;
    out.role = detail::str_of(cJSON_GetObjectItemCaseSensitive(doc, "role"), 16);
    const cJSON* content = cJSON_GetObjectItemCaseSensitive(doc, "content");
    if (cJSON_IsArray(content)) {
      // The array is the raw SDK content: text blocks, tool calls, whatever
      // else a future version adds. Only text is ours; anything unrecognised
      // is skipped rather than stringified, so a new block type shows up as
      // silence, never as JSON on the buddy's face.
      const cJSON* block = nullptr;
      cJSON_ArrayForEach(block, content) {
        if (!cJSON_IsObject(block)) continue;
        const cJSON* type = cJSON_GetObjectItemCaseSensitive(block, "type");
        if (!cJSON_IsString(type) || std::strcmp(type->valuestring, "text") != 0) continue;
        const cJSON* txt = cJSON_GetObjectItemCaseSensitive(block, "text");
        if (!cJSON_IsString(txt)) continue;
        if (!out.text.empty() && out.text.size() < kMaxTurn) out.text.push_back(' ');
        const size_t room = kMaxTurn > out.text.size() ? kMaxTurn - out.text.size() : 0;
        if (room == 0) break;
        out.text += sanitize(txt->valuestring, room);
      }
    }
    cJSON_Delete(doc);
    return true;
  }

  // A snapshot is recognised by carrying at least one field we act on.
  // Without this an empty object would read as "zero sessions, nothing
  // running" and put the buddy to sleep on a stray "{}".
  static const char* const kSnapKeys[] = {"total", "running", "waiting",
                                          "msg",   "entries", "prompt",
                                          "tokens", "tokens_today"};
  bool is_snap = false;
  for (const char* k : kSnapKeys) {
    if (cJSON_HasObjectItem(doc, k)) { is_snap = true; break; }
  }
  if (!is_snap) { cJSON_Delete(doc); return false; }

  out.kind = Frame::kSnapshot;
  Snapshot& s = out.snap;
  s.total = detail::clamp_int(cJSON_GetObjectItemCaseSensitive(doc, "total"), 0, 255, 0);
  s.running = detail::clamp_int(cJSON_GetObjectItemCaseSensitive(doc, "running"), 0, 255, 0);
  s.waiting = detail::clamp_int(cJSON_GetObjectItemCaseSensitive(doc, "waiting"), 0, 255, 0);
  s.tokens = detail::clamp_u32(cJSON_GetObjectItemCaseSensitive(doc, "tokens"), 0);
  s.tokens_today = detail::clamp_u32(cJSON_GetObjectItemCaseSensitive(doc, "tokens_today"), 0);
  s.msg = detail::str_of(cJSON_GetObjectItemCaseSensitive(doc, "msg"), kMaxMsg);

  const cJSON* entries = cJSON_GetObjectItemCaseSensitive(doc, "entries");
  if (cJSON_IsArray(entries)) {
    const cJSON* e = nullptr;
    cJSON_ArrayForEach(e, entries) {
      if (s.entries.size() >= kMaxEntries) break;
      if (!cJSON_IsString(e)) continue;
      s.entries.push_back(sanitize(e->valuestring, kMaxEntry));
    }
  }

  const cJSON* prompt = cJSON_GetObjectItemCaseSensitive(doc, "prompt");
  if (cJSON_IsObject(prompt)) {
    const cJSON* id = cJSON_GetObjectItemCaseSensitive(prompt, "id");
    // No id, no prompt: the id is the whole point -- it is what the decision
    // echoes back, and a decision with the wrong id approves the wrong tool.
    if (cJSON_IsString(id)) {
      s.prompt_id = detail::str_of(id, kMaxId);
      if (!s.prompt_id.empty()) {
        s.has_prompt = true;
        s.prompt_tool = detail::str_of(cJSON_GetObjectItemCaseSensitive(prompt, "tool"), kMaxTool);
        s.prompt_hint = detail::str_of(cJSON_GetObjectItemCaseSensitive(prompt, "hint"), kMaxHint);
      }
    }
  }

  cJSON_Delete(doc);
  return true;
}

// ---------------------------------------------------------------------------
// Folder push: file names
// ---------------------------------------------------------------------------

// REFERENCE.md says to validate file.path before writing, and the reference
// firmware does not -- it snprintf's the peer's string straight into
// "/characters/<name>/<path>". The push is flat (no recursion, dotfiles
// skipped), so the rule is not "reject .." but "accept one plain segment":
// no separators at all, no leading dot, printable ASCII only.
inline bool safe_name(const std::string& p) {
  if (p.empty() || p.size() > kMaxName) return false;
  if (p[0] == '.') return false;
  for (char ch : p) {
    const unsigned char c = static_cast<unsigned char>(ch);
    if (c < 0x20 || c >= 0x7F) return false;
    if (c == '/' || c == '\\' || c == ':') return false;
  }
  return true;
}

// ---------------------------------------------------------------------------
// Bridge -> bus
// ---------------------------------------------------------------------------

// Shaped like buddy::Event on purpose: promoting this into firmware/ is a
// typedef, not a rewrite.
struct Event {
  std::string name;
  std::string payload;
};

// Turns the bridge's level-triggered heartbeat into the edge-triggered facts
// a reflex actually wants. The desktop repeats the same snapshot every 10 s;
// a pack wants "it started waiting", not "it is waiting" six times a minute.
//
// Event names are a PROPOSAL. Nothing publishes them yet -- see the spike's
// README before building against one.
class Watcher {
 public:
  void feed(const Frame& f, uint32_t now_ms, std::vector<Event>& out) {
    if (f.kind == Frame::kNone || f.kind == Frame::kCommand) return;

    last_ms_ = now_ms;
    seen_ = true;
    if (!online_) {
      online_ = true;
      out.push_back({"claude.hello", ""});
    }

    if (f.kind == Frame::kTurn) {
      // The one channel the reference firmware never reads: actual words
      // from the model. Text only, capped, and still untrusted -- a reflex
      // that feeds this to brain.ask must delimit it the way the NFC handler
      // delimits sticker text.
      if (!f.text.empty()) out.push_back({"claude.turn", f.text});
      return;
    }
    if (f.kind != Frame::kSnapshot) return;

    const Snapshot& s = f.snap;

    const bool busy = s.running > 0;
    if (busy != busy_) {
      busy_ = busy;
      out.push_back({busy ? "claude.busy" : "claude.idle",
                     busy ? std::to_string(s.running) : std::string()});
    }

    if (s.has_prompt) {
      if (s.prompt_id != prompt_id_) {
        prompt_id_ = s.prompt_id;
        // Two events for one thing, deliberately -- the same split as
        // nfc.tag/nfc.text. claude.prompt carries WHICH tool (a short name
        // the desktop chose); claude.hint carries the command line, which is
        // arbitrary text a reflex must treat as data. claude.prompt always
        // arrives first, so a reflex can stash it and use it when the hint
        // lands. A prompt with no hint emits only the first.
        out.push_back({"claude.prompt", s.prompt_tool});
        if (!s.prompt_hint.empty()) out.push_back({"claude.hint", s.prompt_hint});
      }
    } else if (!prompt_id_.empty()) {
      prompt_id_.clear();
      out.push_back({"claude.resolved", ""});
    }
  }

  // Call from the same place that pumps the bus. Wrap-safe: millis() rolls
  // over every ~49 days and unsigned subtraction is right across the seam.
  void tick(uint32_t now_ms, std::vector<Event>& out) {
    if (!online_ || !seen_) return;
    if (static_cast<uint32_t>(now_ms - last_ms_) <= kSilenceMs) return;
    online_ = false;
    // A pending prompt does not become "resolved" because the link died --
    // nobody answered it. claude.gone is the stronger fact and the only one
    // we publish; a reflex showing an approval screen tears it down on that.
    prompt_id_.clear();
    busy_ = false;
    out.push_back({"claude.gone", ""});
  }

  bool online() const { return online_; }
  const std::string& pending_prompt() const { return prompt_id_; }

 private:
  bool online_ = false;
  bool seen_ = false;
  bool busy_ = false;
  uint32_t last_ms_ = 0;
  std::string prompt_id_;
};

// ---------------------------------------------------------------------------
// Bus -> bridge
// ---------------------------------------------------------------------------

// The only thing the device ever sends that changes anything on the desktop.
// The id came from the peer and goes straight back into a JSON string built
// by hand, so it is re-screened here rather than trusted from decode().
//
// Who is allowed to call this is an architecture decision, not a detail: see
// the README. Short version -- firmware calls it on a physical gesture, packs
// never do.
inline std::string encode_decision(const std::string& id, bool allow) {
  const std::string safe = json_safe(id, kMaxId);
  if (safe.empty()) return std::string();
  return "{\"cmd\":\"permission\",\"id\":\"" + safe + "\",\"decision\":\"" +
         (allow ? "once" : "deny") + "\"}\n";
}

}  // namespace claudebridge
}  // namespace buddy
