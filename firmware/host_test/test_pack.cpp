// Pack JSON -> expression/mood tables, under ASan and UBSan.
//
// A pack is untrusted input: they get shared, mailed, and (once #21 lands)
// installed from a card someone leaves on your desk. The bar is that a hostile
// or merely broken pack yields a dull face, never a crash and never a renderer
// handed nonsense. So the interesting cases here are not the happy path --
// they are wrong types, absurd numbers, and truncated files.
#include "pack_parse.h"

#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

using namespace buddy;
using namespace buddy::packparse;

static int checks = 0;
#define CHECK(c) do { checks++; if (!(c)) { \
  std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); return 1; } } while (0)

static int test_expressions() {
  std::vector<Emotion> v;

  // Happy path: every field honoured.
  CHECK(parse_expressions(
      R"({"neutral":{"eye":{"width":26,"height":30,"openness":100,"lift":2,"brow":-1},
          "blink_ms":3800,"color":"#00beff","mood":"calm"}})", v));
  CHECK(v.size() == 1);
  CHECK(v[0].name == "neutral");
  CHECK(v[0].eye.width == 26 && v[0].eye.height == 30);
  CHECK(v[0].eye.lift == 2 && v[0].eye.brow == -1);
  CHECK(v[0].blink_period_ms == 3800);
  CHECK(v[0].r == 0x00 && v[0].g == 0xbe && v[0].b == 0xff);
  CHECK(v[0].mood == "calm");

  // Colour without '#', and upper case.
  CHECK(parse_expressions(R"({"a":{"color":"FF8800"}})", v));
  CHECK(v[0].r == 0xff && v[0].g == 0x88 && v[0].b == 0x00);

  // Absent fields fall back to the struct defaults, they do not zero out.
  CHECK(parse_expressions(R"({"bare":{}})", v));
  CHECK(v[0].eye.width == 26 && v[0].blink_period_ms == 3800);
  CHECK(v[0].g == 190 && v[0].b == 255);

  // Wrong types are ignored, not coerced: a string where a number belongs must
  // not become 0 and hand the renderer a zero-width eye.
  CHECK(parse_expressions(R"({"a":{"eye":{"width":"wide"},"blink_ms":[1,2]}})", v));
  CHECK(v[0].eye.width == 26);
  CHECK(v[0].blink_period_ms == 3800);

  // eye as a non-object is skipped rather than walked.
  CHECK(parse_expressions(R"({"a":{"eye":42}})", v));
  CHECK(v[0].eye.width == 26);

  // Absurd numbers clamp to what the renderer survives.
  CHECK(parse_expressions(
      R"({"a":{"eye":{"width":99999,"openness":-40,"brow":7,"lift":-3},"blink_ms":1}})", v));
  CHECK(v[0].eye.width == 120);
  CHECK(v[0].eye.openness == 1);   // 1, not 0 -- see the blank-panel case below
  CHECK(v[0].eye.brow == 1);
  CHECK(v[0].eye.lift == 0);
  CHECK(v[0].blink_period_ms == 200);

  // A bad colour leaves the default rather than taking the entry down.
  CHECK(parse_expressions(R"({"a":{"color":"#gggggg"}})", v));
  CHECK(v[0].g == 190);
  CHECK(parse_expressions(R"({"a":{"color":"#fff"}})", v));
  CHECK(v[0].g == 190);
  CHECK(parse_expressions(R"({"a":{"color":""}})", v));
  CHECK(v[0].g == 190);

  // Accented names survive byte-for-byte -- packs are authored in Spanish and
  // the filename of the phrase bank is derived from this string.
  CHECK(parse_expressions("{\"hura\xc3\xb1o\":{}}", v));
  CHECK(v[0].name == "hura\xc3\xb1o");

  // Non-object entries are skipped, and the rest of the pack still loads.
  CHECK(parse_expressions(R"({"a":1,"b":{},"c":"x"})", v));
  CHECK(v.size() == 1 && v[0].name == "b");

  // Rejections.
  CHECK(!parse_expressions("", v));
  CHECK(!parse_expressions("{", v));
  CHECK(!parse_expressions("[]", v));
  CHECK(!parse_expressions("null", v));
  CHECK(!parse_expressions("{}", v));           // parses, but yields nothing
  CHECK(!parse_expressions(R"({"":{}})", v));   // empty name is not a name

  // The entry cap holds.
  std::string big = "{";
  for (int i = 0; i < 200; i++) {
    if (i) big += ",";
    big += "\"e" + std::to_string(i) + "\":{}";
  }
  big += "}";
  CHECK(parse_expressions(big.c_str(), v));
  CHECK(v.size() == kMaxEntries);

  // openness 0 is the field that blanks the whole panel: the renderer divides
  // by height * openness to place the squint, so zero became an infinite
  // offset that pushed every pixel of BOTH eyes out of range. Every field was
  // individually inside its range, which is why per-field clamps missed it.
  CHECK(parse_expressions(R"({"neutral":{"eye":{"openness":0,"lift":14}}})", v));
  CHECK(v[0].eye.openness >= 1);
  CHECK(parse_expressions(R"({"neutral":{"eye":{"openness":1e999}}})", v));
  CHECK(v[0].eye.openness == 100);
  CHECK(parse_expressions(R"({"a":{"blink_ms":1e999}})", v));
  CHECK(v[0].blink_period_ms == 60000);
  CHECK(parse_expressions(R"({"a":{"blink_ms":-1e999}})", v));
  CHECK(v[0].blink_period_ms == 200);

  // A name becomes lines/<name>.txt, so it is screened, not just non-empty.
  CHECK(!parse_expressions(R"({"a/b":{}})", v));
  CHECK(!parse_expressions(R"({"../../flash/reflexes/main":{}})", v));
  CHECK(!parse_expressions(R"({"..":{}})", v));
  // Accented and ñ names are the point of the format and must survive.
  CHECK(parse_expressions(R"({"neutral":{},"huraño":{"mood":"fuego"}})", v));
  CHECK(v.size() == 2 && v[1].name == "huraño");

  // register is postponed (#16): a pack may still declare it, and the loader
  // ignores it the way it ignores any key it does not know.
  CHECK(parse_expressions(R"({"neutral":{"register":"seco","blink_ms":900}})", v));
  CHECK(v.size() == 1 && v[0].blink_period_ms == 900);

  // The tables refuse a swap once the render task is up, and refusing leaves
  // the previous table exactly as it was.
  {
    std::vector<Emotion> good;
    CHECK(parse_expressions(R"({"neutral":{"blink_ms":1234}})", good));
    CHECK(set_emotions(good));
    CHECK(emotion_count() == 1);
    freeze_emotions();
    std::vector<Emotion> other;
    CHECK(parse_expressions(R"({"neutral":{"blink_ms":4321}})", other));
    CHECK(!set_emotions(other));
    CHECK(emotions()[0].blink_period_ms == 1234);

    std::vector<Mood> m;
    CHECK(parse_moods(R"({"solo":{"anim":"spin"}})", m, /*bare_map=*/true));
    CHECK(set_moods(m));
    freeze_moods();
    CHECK(!set_moods(m));
  }

  // A table with no "neutral" is refused: the renderer starts there.
  {
    std::vector<Emotion> no_neutral;
    CHECK(parse_expressions(R"({"happy":{}})", no_neutral));
    CHECK(!set_emotions(no_neutral));
  }
  return 0;
}

static int test_moods() {
  std::vector<Mood> v;

  // Wrapped in a manifest, which is how pack.json actually ships it.
  CHECK(parse_moods(
      R"({"id":"zero","moods":{"fuego":{"anim":"pulse","period_ms":1800,
          "floor":0.1,"dir":"ccw","colors":["#ff3300","#ff8800"]}}})", v));
  CHECK(v.size() == 1);
  CHECK(v[0].name == "fuego");
  CHECK(v[0].anim == Anim::Pulse);
  CHECK(v[0].period_ms == 1800);
  CHECK(v[0].dir == -1);
  CHECK(v[0].colors.size() == 2);
  CHECK(v[0].colors[0].r == 0xff && v[0].colors[1].g == 0x88);

  // A bare map is accepted too.
  CHECK(parse_moods(R"({"off":{"anim":"off"}})", v, /*bare_map=*/true));
  CHECK(v[0].anim == Anim::Off);

  // Every primitive resolves, and an unknown one falls back to Breathe --
  // something visible and slow, never something fast or dark.
  CHECK(parse_moods(R"({"a":{"anim":"solid"}})", v, /*bare_map=*/true)); CHECK(v[0].anim == Anim::Solid);
  CHECK(parse_moods(R"({"a":{"anim":"spin"}})", v, /*bare_map=*/true));  CHECK(v[0].anim == Anim::Spin);
  CHECK(parse_moods(R"({"a":{"anim":"nope"}})", v, /*bare_map=*/true));  CHECK(v[0].anim == Anim::Breathe);
  CHECK(parse_moods(R"({"a":{}})", v, /*bare_map=*/true));               CHECK(v[0].anim == Anim::Breathe);
  CHECK(parse_moods(R"({"a":{"anim":"SPIN"}})", v, /*bare_map=*/true));  CHECK(v[0].anim == Anim::Breathe);

  // No colours means "follow the face", which is the empty vector.
  CHECK(parse_moods(R"({"a":{}})", v, /*bare_map=*/true));
  CHECK(v[0].colors.empty());

  // A period below one frame would be a strobe, not an animation.
  // The floor is two ring frames, not one: at one frame the phase advances by
  // exactly 1.0 and floor() puts it back to zero, so the ring FREEZES.
  CHECK(parse_moods(R"({"a":{"period_ms":0}})", v, /*bare_map=*/true));   CHECK(v[0].period_ms == kMinPeriodMs);
  CHECK(parse_moods(R"({"a":{"period_ms":-5}})", v, /*bare_map=*/true));  CHECK(v[0].period_ms == kMinPeriodMs);
  CHECK(kMinPeriodMs >= 2 * kRingFrameMs);
  CHECK(parse_moods(R"({"a":{"floor":9}})", v, /*bare_map=*/true));       CHECK(v[0].floor == 1.0f);
  CHECK(parse_moods(R"({"a":{"floor":-9}})", v, /*bare_map=*/true));      CHECK(v[0].floor == 0.0f);

  // Bad colours are dropped individually; good ones in the same array survive.
  CHECK(parse_moods(R"({"a":{"colors":["#zzz","#00ff00",7,null,"#0000ff"]}})", v, /*bare_map=*/true));
  CHECK(v[0].colors.size() == 2);
  CHECK(v[0].colors[0].g == 0xff && v[0].colors[1].b == 0xff);

  // colors of the wrong shape entirely.
  CHECK(parse_moods(R"({"a":{"colors":"#ff0000"}})", v, /*bare_map=*/true));
  CHECK(v[0].colors.empty());

  // The colour cap holds.
  std::string many = R"({"a":{"colors":[)";
  for (int i = 0; i < 40; i++) many += (i ? ",\"#010203\"" : "\"#010203\"");
  many += "]}}";
  CHECK(parse_moods(many.c_str(), v, /*bare_map=*/true));
  CHECK(v[0].colors.size() == kMaxColors);

  CHECK(!parse_moods("{", v, /*bare_map=*/true));
  CHECK(!parse_moods(R"({"moods":{}})", v, /*bare_map=*/true));

  // A manifest is NOT a bare mood map. Read as one, every top-level key became
  // a mood: {"id":"zero","expressions":{...}} produced a mood called
  // "expressions" and replaced all four built-ins. One typo -- "mood" for
  // "moods" -- was enough, and the ring then sat on it forever.
  CHECK(!parse_moods(R"({"id":"zero","name":"B","expressions":{"map":"x"}})", v));
  CHECK(!parse_moods(R"({"id":"zero","mood":{"fuego":{"anim":"pulse"}}})", v));
  CHECK(!parse_moods(R"({"moods":42,"expressions":{"map":"x"}})", v));
  // With the flag it is still accepted, which is what a standalone file needs.
  CHECK(parse_moods(R"({"solo":{"anim":"spin"}})", v, /*bare_map=*/true));

  // dir is reported, not silently defaulted.
  CHECK(parse_moods(R"({"a":{"dir":"ccw"}})", v, /*bare_map=*/true));
  CHECK(v[0].dir == -1 && !v[0].unknown_dir);
  CHECK(parse_moods(R"({"a":{"dir":"cw"}})", v, /*bare_map=*/true));
  CHECK(v[0].dir == 1 && !v[0].unknown_dir);
  CHECK(parse_moods(R"({"a":{"dir":"left"}})", v, /*bare_map=*/true));
  CHECK(v[0].dir == 1 && v[0].unknown_dir);

  // 1e999 is valid JSON syntax; cJSON stores +inf with type Number, so this is
  // real input, not a thought experiment. Collapsing a non-finite to the LOW
  // end inverted every clamp -- period_ms landed on the strobe the range
  // exists to forbid.
  CHECK(parse_moods(R"({"a":{"period_ms":1e999}})", v, /*bare_map=*/true));
  CHECK(v[0].period_ms == 600000);
  CHECK(parse_moods(R"({"a":{"period_ms":-1e999}})", v, /*bare_map=*/true));
  CHECK(v[0].period_ms == kMinPeriodMs);
  CHECK(parse_moods(R"({"a":{"floor":1e999}})", v, /*bare_map=*/true));
  CHECK(v[0].floor == 1.0f);

  // Names are capped and screened: one becomes lines/<name>.txt.
  CHECK(!parse_moods(R"({"a/b":{"anim":"spin"}})", v, /*bare_map=*/true));
  CHECK(!parse_moods(R"({"../../flash/reflexes/main":{}})", v, /*bare_map=*/true));
  {
    const std::string big = std::string("{\"") + std::string(200, 'x') + "\":{}}";
    CHECK(!parse_moods(big.c_str(), v, /*bare_map=*/true));
  }

  // Nesting depth is checked BEFORE cJSON sees the document. cJSON_Parse is
  // recursive at ~96 bytes of stack per level against an 8 KB main task, so
  // ~85 levels panics inside the parser -- before the face exists, from a
  // pack that persists. A permanent boot loop out of 200 bytes.
  {
    const std::string deep = std::string(200, '[') + std::string(200, ']');
    CHECK(!parse_moods(deep.c_str(), v, /*bare_map=*/true));
    std::vector<Emotion> e;
    CHECK(!parse_expressions(deep.c_str(), e));
    // A bracket inside a string must not count towards the depth.
    CHECK(parse_moods(R"({"a":{"anim":"spin","dir":"[[[[[[[[[[cw"}})", v,
                      /*bare_map=*/true));
  }
  return 0;
}

int main() {
  if (test_expressions()) return 1;
  if (test_moods()) return 1;
  std::printf("pack parser: %d checks passed\n", checks);
  return 0;
}
