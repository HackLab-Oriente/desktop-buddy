#pragma once
// The parametric face model — renderer-agnostic. Expressions are data (eye
// geometry + blink temperament + colour); the renderer maps them to whatever
// panel it drives, and the colour is shared with the LED ring so the eyes and
// the halo always agree.
//
// The table used to be a C++ constant, which made a group decision one
// person's call. It is now a runtime table: the built-ins below are only the
// fallback, and a pack replaces them at boot (see components/pack).
//
// Still called "emotion" on purpose. Renaming it is #16's decision, not
// firmware's, and `face.emotion` is a published bus event — see
// docs/event-registry.md.
#include <cstdint>
#include <string>
#include <vector>

namespace buddy {

// openness: 0..100 (% of height the lids open). lift: rows the lower lid rises
// ("happy squint"). brow: +1 slants upper lids toward the nose (angry),
// -1 toward the temples (sad), 0 = none. Values are in the abstract 0..100
// model space; renderers scale to their panel.
struct EyeStyle {
  int width = 26, height = 30, openness = 100, lift = 0, brow = 0;
};

struct Emotion {
  std::string name;
  EyeStyle eye;
  int blink_period_ms = 3800;      // mean ms between blinks — sets temperament
  uint8_t r = 0, g = 190, b = 255; // the eye tint AND the LED ring colour
  std::string mood;                // LED mood to request with it; empty = none
};

// The active table. Never empty: falls back to the built-ins.
const Emotion* emotions();
int emotion_count();

// Index into emotions() by name, or -1 if unknown.
int emotion_index(const char* name);

// Replace the table. Ignored (and reported false) if `v` is empty or has no
// entry named "neutral" — the renderer starts there, and a pack that cannot
// render a neutral face is worse than no pack at all.
//
// BOOT ONLY, and enforced: once face_start() has run, freeze_emotions() is
// called and any later swap is refused and logged. emotions() hands out a
// pointer into the table's storage and the render task holds references to
// entries for a whole frame, so a swap under it is a use-after-free. The
// runtime pack switch needs a snapshot accessor rather than this call; the
// guard is here so that until then a mistake is a log line, not a corruption.
bool set_emotions(std::vector<Emotion> v);
void freeze_emotions();

}  // namespace buddy
