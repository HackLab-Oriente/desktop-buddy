// The Claude desktop bridge decoder, under ASan and UBSan.
//
// The bridge is untrusted input in the strongest sense we have: it is not a
// file someone mailed us, it is a live radio peer. Until LE Secure
// Connections bonding is in, "the desktop app" is anyone within a few metres
// holding a cheap dongle. So the interesting cases here are not the happy
// path -- they are wrong types, absurd numbers, truncated UTF-8, nesting
// bombs, a line that never ends, and a file name that wants to be a path.
//
//   c++ -std=c++17 -Wall -Wextra -fsanitize=address,undefined
//       -fno-sanitize-recover=all -I/usr/include/cjson
//       test_claude_bridge.cpp -lcjson -o test_claude_bridge && ./test_claude_bridge
#include "claude_bridge.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace buddy::claudebridge;

static int checks = 0;
#define CHECK(c) do { checks++; if (!(c)) { \
  std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); return 1; } } while (0)

// Names of the events a call produced, joined -- easier to assert on than
// poking at indices, and it catches ORDER, which is part of the contract.
static std::string names(const std::vector<Event>& v) {
  std::string s;
  for (const Event& e : v) { if (!s.empty()) s += ","; s += e.name; }
  return s;
}

// ---------------------------------------------------------------------------

static int test_snapshot_happy_path() {
  // Verbatim from REFERENCE.md, so this test fails if their document and our
  // reading of it ever part ways.
  const std::string line = R"({"total":3,"running":1,"waiting":1,)"
      R"("msg":"approve: Bash",)"
      R"("entries":["10:42 git push","10:41 yarn test","10:39 reading file..."],)"
      R"("tokens":184502,"tokens_today":31200,)"
      R"("prompt":{"id":"req_abc123","tool":"Bash","hint":"rm -rf /tmp/foo"}})";

  Frame f;
  CHECK(decode(line, f));
  CHECK(f.kind == Frame::kSnapshot);
  CHECK(f.snap.total == 3 && f.snap.running == 1 && f.snap.waiting == 1);
  CHECK(f.snap.msg == "approve: Bash");
  CHECK(f.snap.entries.size() == 3);
  CHECK(f.snap.entries[0] == "10:42 git push");
  CHECK(f.snap.tokens == 184502u && f.snap.tokens_today == 31200u);
  CHECK(f.snap.has_prompt);
  CHECK(f.snap.prompt_id == "req_abc123");
  CHECK(f.snap.prompt_tool == "Bash");
  CHECK(f.snap.prompt_hint == "rm -rf /tmp/foo");
  return 0;
}

static int test_frame_kinds() {
  Frame f;

  // Time sync, one-shot on connect.
  CHECK(decode(R"({"time":[1775731234,-25200]})", f));
  CHECK(f.kind == Frame::kTime);
  CHECK(f.epoch == 1775731234);
  CHECK(f.tz_offset == -25200);

  // A timezone of 9e9 seconds is not a timezone.
  CHECK(decode(R"({"time":[1775731234,9000000000]})", f));
  CHECK(f.tz_offset == 50400);

  // Turn event: text blocks joined, everything else skipped.
  CHECK(decode(R"({"evt":"turn","role":"assistant","content":[)"
               R"({"type":"text","text":"Ready when you are."},)"
               R"({"type":"tool_use","name":"Bash","input":{"cmd":"ls"}},)"
               R"({"type":"text","text":"Running it."}]})", f));
  CHECK(f.kind == Frame::kTurn);
  CHECK(f.role == "assistant");
  CHECK(f.text == "Ready when you are. Running it.");

  // A block type we have never seen contributes nothing -- it must not leak
  // JSON onto the face.
  CHECK(decode(R"({"evt":"turn","content":[{"type":"thinking","text":"hmm"}]})", f));
  CHECK(f.kind == Frame::kTurn);
  CHECK(f.text.empty());

  // Commands.
  CHECK(decode(R"({"cmd":"owner","name":"Felix"})", f));
  CHECK(f.kind == Frame::kCommand && f.cmd == "owner" && f.name == "Felix");
  CHECK(decode(R"({"cmd":"file","path":"manifest.json","size":412})", f));
  CHECK(f.kind == Frame::kCommand && f.name == "manifest.json" && f.size == 412u);
  CHECK(decode(R"({"cmd":"char_begin","name":"bufo","total":184320})", f));
  CHECK(f.size == 184320u);
  return 0;
}

static int test_garbage() {
  Frame f;

  // Nothing to act on.
  CHECK(!decode("", f));
  CHECK(!decode("{", f));
  CHECK(!decode("not json at all", f));
  CHECK(!decode("[1,2,3]", f));
  CHECK(!decode("null", f));
  CHECK(!decode(R"({"total":1)", f));                 // truncated
  CHECK(!decode(R"({"hello":"world"})", f));          // valid JSON, not ours
  CHECK(!decode("{}", f));                            // and neither is empty
  return 0;
}

static int test_wrong_types() {
  Frame f;

  // Wrong types are ignored, never coerced. "many" sessions is zero sessions,
  // not a crash and not a 1.
  CHECK(decode(R"({"total":"many","running":true,"msg":42,"entries":"nope"})", f));
  CHECK(f.snap.total == 0);
  CHECK(f.snap.running == 0);
  CHECK(f.snap.msg.empty());
  CHECK(f.snap.entries.empty());

  // A prompt that is not an object, or has no id, is not a prompt. This one
  // matters more than it looks: has_prompt drives an approval screen, and an
  // approval screen with no id has no answer to send.
  CHECK(decode(R"({"total":1,"prompt":7})", f));
  CHECK(!f.snap.has_prompt);
  CHECK(decode(R"({"total":1,"prompt":{"tool":"Bash","hint":"rm -rf /"}})", f));
  CHECK(!f.snap.has_prompt);
  CHECK(decode(R"({"total":1,"prompt":{"id":""}})", f));
  CHECK(!f.snap.has_prompt);
  CHECK(decode(R"({"total":1,"prompt":{"id":"req_1"}})", f));
  CHECK(f.snap.has_prompt && f.snap.prompt_tool.empty() && f.snap.prompt_hint.empty());
  return 0;
}

static int test_absurd_numbers() {
  Frame f;

  // Clamped, not wrapped. 99999999999 into an int is UB waiting to happen and
  // UBSan is watching.
  CHECK(decode(R"({"total":99999999999,"running":-5,"waiting":1e300})", f));
  CHECK(f.snap.total == 255);
  CHECK(f.snap.running == 0);
  CHECK(f.snap.waiting == 255);

  CHECK(decode(R"({"tokens":-1,"tokens_today":1e300,"total":1})", f));
  CHECK(f.snap.tokens == 0u);
  CHECK(f.snap.tokens_today == 4294967295u);
  return 0;
}

static int test_nesting_bomb() {
  // 200 levels of nothing. The pack parser found this the expensive way:
  // cJSON_Parse recurses, the main task has 8 KB of stack, and a deep enough
  // document panics INSIDE the parser -- from a file that persists across
  // reboots. Over the air it is worse: it costs the sender one packet.
  std::string bomb = R"({"total":1,"x":)";
  for (int i = 0; i < 200; i++) bomb += "[";
  for (int i = 0; i < 200; i++) bomb += "]";
  bomb += "}";

  Frame f;
  CHECK(!decode(bomb, f));

  // A bracket inside a string is not nesting.
  CHECK(decode(R"({"total":1,"msg":"[[[[[[[[[[[[[[[["})", f));
  CHECK(f.snap.msg == "[[[[[[[[[[[[[[[[");
  return 0;
}

static int test_caps() {
  Frame f;

  // Long strings are cut, not rejected -- a chatty transcript line is normal
  // traffic, not an attack.
  std::string long_hint(500, 'x');
  CHECK(decode(R"({"total":1,"prompt":{"id":"r1","hint":")" + long_hint + R"("}})", f));
  CHECK(f.snap.prompt_hint.size() == kMaxHint);

  // More entries than we keep.
  std::string many = R"({"total":1,"entries":[)";
  for (int i = 0; i < 40; i++) many += (i ? ",\"line\"" : "\"line\"");
  many += "]}";
  CHECK(decode(many, f));
  CHECK(f.snap.entries.size() == kMaxEntries);

  // A turn made of many text blocks stops at the cap without running past it.
  std::string blocks = R"({"evt":"turn","content":[)";
  for (int i = 0; i < 60; i++) {
    if (i) blocks += ",";
    blocks += R"({"type":"text","text":"aaaaaaaaaa"})";
  }
  blocks += "]}";
  CHECK(decode(blocks, f));
  CHECK(f.text.size() <= kMaxTurn);
  return 0;
}

static int test_text_hygiene() {
  Frame f;

  // Control bytes never reach the renderer or the log. A newline in
  // particular: our own outbound frames are newline-delimited.
  CHECK(decode(R"({"total":1,"msg":"a\nb\tc"})", f));
  CHECK(f.snap.msg == "abc");

  // Accents survive. This is a Spanish-speaking lab; "ñ" is not an edge case.
  CHECK(decode(R"({"total":1,"msg":"pequeño buddy"})", f));
  CHECK(f.snap.msg == "pequeño buddy");

  // The cap lands on a character boundary, never inside one. "ñ" is two
  // bytes: with room for one, it must be dropped whole.
  {
    std::string s = sanitize("aaañ", 4);
    CHECK(s == "aaa");
    CHECK(sanitize("aaañ", 5) == "aaañ");
    CHECK(sanitize("ñññññ", 5) == "ññ");
  }

  // Malformed UTF-8 loses the bad bytes and keeps the rest -- showing what we
  // can beats going mute.
  {
    const char lone_tail[] = {'a', (char)0x80, 'b', 0};
    CHECK(sanitize(lone_tail, 16) == "ab");
    const char truncated[] = {'a', (char)0xC3, 0};   // lead byte, no tail
    CHECK(sanitize(truncated, 16) == "a");
    const char overlong_lead[] = {(char)0xF0, (char)0x9F, 'x', 0};
    CHECK(sanitize(overlong_lead, 16) == "x");
  }

  // json_safe is stricter: it also drops what would break a hand-built frame.
  CHECK(json_safe("he said \"hi\" \\ bye", 64) == "he said hi  bye");
  return 0;
}

static int test_line_framing() {
  LineReader r;
  std::vector<std::string> got;
  auto sink = [&](const std::string& l) { got.push_back(l); };

  auto feed = [&](const std::string& s) {
    r.feed(reinterpret_cast<const uint8_t*>(s.data()), s.size(), sink);
  };

  // A line split across three notifications is one line.
  feed("{\"tot");
  feed("al\":1}");
  CHECK(got.empty());
  feed("\n");
  CHECK(got.size() == 1 && got[0] == R"({"total":1})");

  // Blank lines and \r\n produce nothing extra.
  got.clear();
  feed("\r\n\n{\"total\":2}\r\n");
  CHECK(got.size() == 1 && got[0] == R"({"total":2})");

  // The one that matters: an over-long line is dropped WHOLE, and the stream
  // re-syncs on the next newline. A truncating reader would hand us a shorter
  // document that still parses -- the sender choosing what survives the cut --
  // and would then start the following frame mid-line, forever.
  got.clear();
  feed(std::string(kMaxLine + 500, 'A'));
  CHECK(got.empty());
  CHECK(r.buffered() == 0);          // and it is not sitting in RAM
  feed("more of the same, still no newline");
  CHECK(got.empty());
  feed("\n{\"total\":3}\n");
  CHECK(got.size() == 1 && got[0] == R"({"total":3})");
  CHECK(r.dropped() == 1);

  // A peer that never sends a newline cannot grow the buffer without bound.
  got.clear();
  for (int i = 0; i < 100; i++) feed(std::string(1024, 'B'));
  CHECK(r.buffered() <= kMaxLine);
  CHECK(got.empty());
  return 0;
}

static int test_watcher() {
  Watcher w;
  Frame f;
  std::vector<Event> ev;

  // First contact.
  CHECK(decode(R"({"total":0,"running":0})", f));
  w.feed(f, 1000, ev);
  CHECK(names(ev) == "claude.hello");
  CHECK(w.online());

  // The same snapshot ten seconds later is not news. The desktop repeats it
  // every 10 s whether or not anything changed; a pack that got an event each
  // time would have no way to tell a keepalive from a change.
  ev.clear();
  w.feed(f, 11000, ev);
  CHECK(ev.empty());

  // Work starts, then stops: one edge each way, payload carries the count so
  // the pack decides what "busy" means. (The reference firmware hardcodes
  // running >= 3; that is a product opinion and it belongs in the pack.)
  ev.clear();
  CHECK(decode(R"({"total":2,"running":2})", f));
  w.feed(f, 12000, ev);
  CHECK(names(ev) == "claude.busy");
  CHECK(ev[0].payload == "2");

  ev.clear();
  CHECK(decode(R"({"total":3,"running":3})", f));   // still busy, no new edge
  w.feed(f, 13000, ev);
  CHECK(ev.empty());

  ev.clear();
  CHECK(decode(R"({"total":1,"running":0})", f));
  w.feed(f, 14000, ev);
  CHECK(names(ev) == "claude.idle");

  // A permission prompt: identity first, then the attacker-controlled text,
  // the same split and the same ordering guarantee as nfc.tag/nfc.text.
  ev.clear();
  CHECK(decode(R"({"total":1,"waiting":1,)"
               R"("prompt":{"id":"req_1","tool":"Bash","hint":"rm -rf /tmp/foo"}})", f));
  w.feed(f, 15000, ev);
  CHECK(names(ev) == "claude.prompt,claude.hint");
  CHECK(ev[0].payload == "Bash");
  CHECK(ev[1].payload == "rm -rf /tmp/foo");
  CHECK(w.pending_prompt() == "req_1");

  // Repeats of the SAME prompt are silent -- it is up every heartbeat until
  // somebody answers it.
  ev.clear();
  w.feed(f, 16000, ev);
  CHECK(ev.empty());

  // A different prompt is a new fact.
  ev.clear();
  CHECK(decode(R"({"total":1,"waiting":1,"prompt":{"id":"req_2","tool":"Edit"}})", f));
  w.feed(f, 17000, ev);
  CHECK(names(ev) == "claude.prompt");      // no hint, so no claude.hint
  CHECK(ev[0].payload == "Edit");

  // Answered elsewhere (the desktop's own dialog): the prompt disappears.
  ev.clear();
  CHECK(decode(R"({"total":1,"waiting":0})", f));
  w.feed(f, 18000, ev);
  CHECK(names(ev) == "claude.resolved");
  CHECK(w.pending_prompt().empty());

  // Turn text -- the channel the reference firmware documents and never reads.
  ev.clear();
  CHECK(decode(R"({"evt":"turn","role":"assistant",)"
               R"("content":[{"type":"text","text":"Done."}]})", f));
  w.feed(f, 19000, ev);
  CHECK(names(ev) == "claude.turn");
  CHECK(ev[0].payload == "Done.");

  // Silence. 30 s is the bridge's own definition of dead.
  ev.clear();
  w.tick(20000, ev);
  CHECK(ev.empty());
  w.tick(19000 + kSilenceMs, ev);
  CHECK(ev.empty());                        // exactly at the edge is still alive
  w.tick(19000 + kSilenceMs + 1, ev);
  CHECK(names(ev) == "claude.gone");
  CHECK(!w.online());

  // ...and it only fires once.
  ev.clear();
  w.tick(19000 + kSilenceMs + 5000, ev);
  CHECK(ev.empty());

  // Coming back is a fresh hello, and the busy edge fires again because the
  // link dropping reset what we believe.
  ev.clear();
  CHECK(decode(R"({"total":1,"running":1})", f));
  w.feed(f, 60000, ev);
  CHECK(names(ev) == "claude.hello,claude.busy");
  return 0;
}

static int test_watcher_never_speaks_before_it_hears() {
  // tick() on a watcher that has never seen a frame must stay quiet: at boot
  // there is no bridge and "claude.gone" would be a lie the pack acts on.
  Watcher w;
  std::vector<Event> ev;
  w.tick(0, ev);
  w.tick(1000000, ev);
  CHECK(ev.empty());
  CHECK(!w.online());
  return 0;
}

static int test_millis_wraparound() {
  // millis() rolls over every ~49 days. A buddy that has been on a shelf for
  // seven weeks must not decide the bridge died the moment the counter wraps.
  Watcher w;
  Frame f;
  std::vector<Event> ev;

  const uint32_t near_end = 0xFFFFFF00u;
  CHECK(decode(R"({"total":1,"running":0})", f));
  w.feed(f, near_end, ev);
  ev.clear();

  w.tick(0x000000FFu, ev);        // 511 ms later, across the seam
  CHECK(ev.empty());
  CHECK(w.online());

  w.tick(near_end + kSilenceMs + 1, ev);   // wraps too, and this one is real
  CHECK(names(ev) == "claude.gone");
  return 0;
}

static int test_safe_name() {
  // Accept: a plain flat file name, which is all the folder push sends.
  CHECK(safe_name("manifest.json"));
  CHECK(safe_name("idle_0.gif"));
  CHECK(safe_name("a"));

  // Reject: anything that is a path rather than a name.
  CHECK(!safe_name("../../etc/passwd"));
  CHECK(!safe_name("/etc/passwd"));
  CHECK(!safe_name(".."));
  CHECK(!safe_name("a/../b"));
  CHECK(!safe_name("sub/file.gif"));
  CHECK(!safe_name("..\\windows\\system32"));
  CHECK(!safe_name("C:file"));
  CHECK(!safe_name(".hidden"));
  CHECK(!safe_name(""));
  CHECK(!safe_name(std::string(kMaxName + 1, 'a')));

  // Non-ASCII is refused rather than transliterated: the image builder
  // already taught us that an accented filename does not survive the round
  // trip, and a name we cannot reproduce byte-for-byte is a name we cannot
  // delete later.
  CHECK(!safe_name("cañón.gif"));
  {
    const char ctrl[] = {'a', '\n', 'b', 0};
    CHECK(!safe_name(ctrl));
  }

  // And the decoder hands the name through unchanged for this check to see.
  Frame f;
  CHECK(decode(R"({"cmd":"file","path":"../../secret","size":1})", f));
  CHECK(f.kind == Frame::kCommand);
  CHECK(!safe_name(f.name));
  return 0;
}

static int test_encode_decision() {
  CHECK(encode_decision("req_abc123", true) ==
        "{\"cmd\":\"permission\",\"id\":\"req_abc123\",\"decision\":\"once\"}\n");
  CHECK(encode_decision("req_abc123", false) ==
        "{\"cmd\":\"permission\",\"id\":\"req_abc123\",\"decision\":\"deny\"}\n");

  // The id came from the peer and goes back into a frame we build with string
  // concatenation. If it can carry a quote, the peer writes the rest of the
  // frame: an id of `x","decision":"once` would turn our deny into an
  // approval. Screened on the way out, those bytes stay inert payload.
  const std::string forged = encode_decision(R"(x","decision":"once)", false);
  CHECK(forged ==
        "{\"cmd\":\"permission\",\"id\":\"x,decision:once\",\"decision\":\"deny\"}\n");

  // Exactly one frame, terminated -- never two, never none.
  CHECK(std::count(forged.begin(), forged.end(), '\n') == 1);
  CHECK(forged.back() == '\n');

  // And it still means what we meant: parsed back, "once" is a substring of
  // an id and nothing else. Asserting on the spelling above would pass just
  // as well if the escaping were subtly wrong, so ask a parser instead.
  {
    cJSON* d = cJSON_Parse(forged.c_str());
    CHECK(d != nullptr);
    const cJSON* dec = cJSON_GetObjectItemCaseSensitive(d, "decision");
    CHECK(cJSON_IsString(dec) && std::strcmp(dec->valuestring, "deny") == 0);
    const cJSON* id = cJSON_GetObjectItemCaseSensitive(d, "id");
    CHECK(cJSON_IsString(id) && std::strcmp(id->valuestring, "x,decision:once") == 0);
    cJSON_Delete(d);
  }

  // Nothing to answer means nothing is sent.
  CHECK(encode_decision("", true).empty());
  CHECK(encode_decision("\"\"\\\\", true).empty());
  return 0;
}

// Full round trip: bytes off the radio, in the fragments a 185-byte MTU
// actually delivers, through to the events a reflex would see.
static int test_end_to_end() {
  const std::string wire =
      R"({"time":[1775731234,-25200]})" "\n"
      R"({"total":1,"running":1,"msg":"working"})" "\n"
      R"(garbage that is not json)" "\n"
      R"({"total":1,"running":0,"waiting":1,"prompt":{"id":"req_9","tool":"Bash","hint":"git push --force"}})" "\n"
      R"({"evt":"turn","role":"assistant","content":[{"type":"text","text":"Pushed."}]})" "\n";

  LineReader r;
  Watcher w;
  std::vector<Event> ev;
  uint32_t clock = 5000;

  auto on_line = [&](const std::string& line) {
    Frame f;
    if (decode(line, f)) w.feed(f, clock, ev);
    clock += 100;
  };

  for (size_t i = 0; i < wire.size(); i += 182) {   // the real notify chunk
    const size_t n = std::min<size_t>(182, wire.size() - i);
    r.feed(reinterpret_cast<const uint8_t*>(wire.data() + i), n, on_line);
  }

  CHECK(names(ev) == "claude.hello,claude.busy,claude.idle,claude.prompt,"
                     "claude.hint,claude.turn");
  CHECK(w.pending_prompt() == "req_9");
  CHECK(!encode_decision(w.pending_prompt(), false).empty());
  return 0;
}

int main() {
  if (test_snapshot_happy_path()) return 1;
  if (test_frame_kinds()) return 1;
  if (test_garbage()) return 1;
  if (test_wrong_types()) return 1;
  if (test_absurd_numbers()) return 1;
  if (test_nesting_bomb()) return 1;
  if (test_caps()) return 1;
  if (test_text_hygiene()) return 1;
  if (test_line_framing()) return 1;
  if (test_watcher()) return 1;
  if (test_watcher_never_speaks_before_it_hears()) return 1;
  if (test_millis_wraparound()) return 1;
  if (test_safe_name()) return 1;
  if (test_encode_decision()) return 1;
  if (test_end_to_end()) return 1;
  std::printf("ok  %d checks\n", checks);
  return 0;
}
