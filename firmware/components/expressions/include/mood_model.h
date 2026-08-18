#pragma once
// The mood model — what the LED ring does, as data.
//
// The animation vocabulary is a CLOSED short list on purpose. This is 12 LEDs
// updating next to a face that renders in ~30 ms; a mini-language for ring
// animation is a burrow with very little prize. Expressiveness lives in the
// parameters (colours, period, floor, direction). If a pack genuinely needs
// something these five cannot express, add a sixth primitive — not an
// interpreter.
//
// Mood NAMES, by contrast, are open: a pack invents as many as it likes. That
// is what stops led.mood from being a second closed vocabulary competing with
// the expression set (#19).
#include <cstdint>
#include <string>
#include <vector>

namespace buddy {

enum class Anim { Solid, Breathe, Spin, Pulse, Off };

struct Rgb { uint8_t r = 0, g = 0, b = 0; };

struct Mood {
  std::string name;
  Anim anim = Anim::Breathe;
  int period_ms = 3200;   // full cycle; for Spin, time for one lap
  float floor = 0.06f;    // dimmest point of Breathe/Pulse, 0..1
  int dir = 1;            // Spin: +1 clockwise, -1 counter-clockwise
  // Empty means "follow the face" — the ring takes the current expression's
  // colour, which is what keeps eyes and halo agreeing by default. A pack that
  // sets colours here is deliberately breaking that tie.
  std::vector<Rgb> colors;
};

const Mood* moods();
int mood_count();
int mood_index(const char* name);

// Replace the table. Ignored (false) if `v` is empty.
bool set_moods(std::vector<Mood> v);

}  // namespace buddy
