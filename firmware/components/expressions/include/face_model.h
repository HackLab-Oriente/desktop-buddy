#pragma once
// The parametric face model — renderer-agnostic. Emotions are data (eye
// geometry + blink temperament); a renderer maps them to whatever panel it
// drives. Shared by the SSD1306 OLED and GC9A01 round-color backends so both
// speak the same emotional vocabulary. This is the "expression model" the
// architecture doc describes; v1 will move this table into personality packs.
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
};

extern const Emotion kEmotions[];
extern const int kEmotionCount;

// Index into kEmotions by name, or -1 if unknown.
int emotion_index(const char* name);

// 3x5 uppercase bitmap font for the retro text mode. Bit 0b100 = leftmost
// column. glyph_index maps a char to its row in kFont (space for unknown).
extern const uint8_t kFont[][5];
int glyph_index(char c);

}  // namespace buddy
