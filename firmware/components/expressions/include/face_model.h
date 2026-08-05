#pragma once
// The parametric face model — renderer-agnostic. Emotions are data (eye
// geometry + blink temperament); the renderer maps them to whatever panel it
// drives, and the mood color is shared with the LED ring so the eyes and the
// halo always agree. This is the "expression model" the architecture doc
// describes.
//
// This table still living in C++ is a known wart: the set of emotions is a
// group decision, and baking it into firmware makes it one person's call.
// It belongs in personality-pack data. See docs/ideas-exploration.html.
#include <cstdint>

namespace buddy {

// openness: 0..100 (% of height the lids open). lift: rows the lower lid rises
// ("happy squint"). brow: +1 slants upper lids toward the nose (angry),
// -1 toward the temples (sad), 0 = none. Values are in the abstract 0..100
// model space; renderers scale to their panel.
struct EyeStyle {
  int width, height, openness, lift, brow;
};
struct Emotion {
  const char* name;
  EyeStyle eye;
  int blink_period_ms;  // mean ms between blinks — sets temperament
  uint8_t r, g, b;      // mood color — the eye tint AND the LED ring color
};

extern const Emotion kEmotions[];
extern const int kEmotionCount;

// Index into kEmotions by name, or -1 if unknown.
int emotion_index(const char* name);

}  // namespace buddy
