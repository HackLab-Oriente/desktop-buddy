#include "mood_model.h"

#include <utility>

namespace buddy {
namespace {

// The fallback set: exactly the four that were hard-coded before packs, so a
// board with no pack behaves as it always did.
const std::vector<Mood>& builtins() {
  static const std::vector<Mood> v = {
      {"calm",     Anim::Breathe, 3200, 0.06f, 1, {}},
      {"excited",  Anim::Breathe,  700, 0.18f, 1, {}},
      {"thinking", Anim::Spin,     480, 0.0f,  1, {}},
      {"off",      Anim::Off,        0, 0.0f,  1, {}},
  };
  return v;
}

std::vector<Mood>& table() {
  static std::vector<Mood> t = builtins();
  return t;
}

bool s_frozen = false;

}  // namespace

const Mood* moods() { return table().data(); }
int mood_count() { return static_cast<int>(table().size()); }

int mood_index(const char* name) {
  const std::vector<Mood>& t = table();
  for (size_t i = 0; i < t.size(); i++)
    if (t[i].name == name) return static_cast<int>(i);
  return -1;
}

void freeze_moods() { s_frozen = true; }

bool set_moods(std::vector<Mood> v) {
  if (s_frozen) return false;   // the ring holds a reference for a whole frame
  if (v.empty()) return false;
  table() = std::move(v);
  return true;
}

}  // namespace buddy
