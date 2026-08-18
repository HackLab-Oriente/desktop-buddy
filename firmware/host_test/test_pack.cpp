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
          "blink_ms":3800,"color":"#00beff","mood":"calm","register":"llano"}})", v));
  CHECK(v.size() == 1);
  CHECK(v[0].name == "neutral");
  CHECK(v[0].eye.width == 26 && v[0].eye.height == 30);
  CHECK(v[0].eye.lift == 2 && v[0].eye.brow == -1);
  CHECK(v[0].blink_period_ms == 3800);
  CHECK(v[0].r == 0x00 && v[0].g == 0xbe && v[0].b == 0xff);
  CHECK(v[0].mood == "calm");
  CHECK(v[0].register_ == "llano");

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
  CHECK(v[0].eye.openness == 0);
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
  CHECK(parse_moods(R"({"off":{"anim":"off"}})", v));
  CHECK(v[0].anim == Anim::Off);

  // Every primitive resolves, and an unknown one falls back to Breathe --
  // something visible and slow, never something fast or dark.
  CHECK(parse_moods(R"({"a":{"anim":"solid"}})", v)); CHECK(v[0].anim == Anim::Solid);
  CHECK(parse_moods(R"({"a":{"anim":"spin"}})", v));  CHECK(v[0].anim == Anim::Spin);
  CHECK(parse_moods(R"({"a":{"anim":"nope"}})", v));  CHECK(v[0].anim == Anim::Breathe);
  CHECK(parse_moods(R"({"a":{}})", v));               CHECK(v[0].anim == Anim::Breathe);
  CHECK(parse_moods(R"({"a":{"anim":"SPIN"}})", v));  CHECK(v[0].anim == Anim::Breathe);

  // No colours means "follow the face", which is the empty vector.
  CHECK(parse_moods(R"({"a":{}})", v));
  CHECK(v[0].colors.empty());

  // A period below one frame would be a strobe, not an animation.
  CHECK(parse_moods(R"({"a":{"period_ms":0}})", v));   CHECK(v[0].period_ms == 20);
  CHECK(parse_moods(R"({"a":{"period_ms":-5}})", v));  CHECK(v[0].period_ms == 20);
  CHECK(parse_moods(R"({"a":{"floor":9}})", v));       CHECK(v[0].floor == 1.0f);
  CHECK(parse_moods(R"({"a":{"floor":-9}})", v));      CHECK(v[0].floor == 0.0f);

  // Bad colours are dropped individually; good ones in the same array survive.
  CHECK(parse_moods(R"({"a":{"colors":["#zzz","#00ff00",7,null,"#0000ff"]}})", v));
  CHECK(v[0].colors.size() == 2);
  CHECK(v[0].colors[0].g == 0xff && v[0].colors[1].b == 0xff);

  // colors of the wrong shape entirely.
  CHECK(parse_moods(R"({"a":{"colors":"#ff0000"}})", v));
  CHECK(v[0].colors.empty());

  // The colour cap holds.
  std::string many = R"({"a":{"colors":[)";
  for (int i = 0; i < 40; i++) many += (i ? ",\"#010203\"" : "\"#010203\"");
  many += "]}}";
  CHECK(parse_moods(many.c_str(), v));
  CHECK(v[0].colors.size() == kMaxColors);

  CHECK(!parse_moods("{", v));
  CHECK(!parse_moods(R"({"moods":{}})", v));
  return 0;
}

int main() {
  if (test_expressions()) return 1;
  if (test_moods()) return 1;
  std::printf("pack parser: %d checks passed\n", checks);
  return 0;
}
