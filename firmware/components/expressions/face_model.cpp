#include "face_model.h"

#include <cstring>
#include <utility>

namespace buddy {
namespace {

// The fallback set. A board with no pack — or with a pack that fails to
// parse — still has a face. See set_emotions() for why "neutral" is required.
const std::vector<Emotion>& builtins() {
  static const std::vector<Emotion> v = {
      //name          eye {w,h,open,lift,brow}   blink  r    g    b     mood       register
      {"neutral",    {26, 30, 100,  0,  0},      3800,   0, 190, 255, "calm",     "llano"},
      {"happy",      {26, 30, 100, 14,  0},      3000,  40, 235, 120, "excited",  "cálido"},
      {"curious",    {30, 34, 100,  0,  0},      2600,   0, 210, 235, "thinking", "curioso"},
      {"sleepy",     {26, 30,  35,  0,  0},      6000,  90,  90, 200, "off",      "somnoliento"},
      {"surprised",  {34, 40, 100,  0,  0},      5000, 150, 240, 255, "excited",  "urgente"},
      {"angry",      {28, 28, 100,  0,  1},      3200, 255,  50,  25, "calm",     "seco"},
      {"sad",        {24, 26,  75,  0, -1},      5200,  50, 110, 255, "calm",     "somnoliento"},
      {"suspicious", {26, 30,  55,  0,  1},      2200, 255, 180,  20, "calm",     "seco"},
  };
  return v;
}

std::vector<Emotion>& table() {
  static std::vector<Emotion> t = builtins();
  return t;
}

}  // namespace

const Emotion* emotions() { return table().data(); }
int emotion_count() { return static_cast<int>(table().size()); }

int emotion_index(const char* name) {
  const std::vector<Emotion>& t = table();
  for (size_t i = 0; i < t.size(); i++)
    if (t[i].name == name) return static_cast<int>(i);
  return -1;
}

bool set_emotions(std::vector<Emotion> v) {
  if (v.empty()) return false;
  bool has_neutral = false;
  for (const Emotion& e : v)
    if (e.name == "neutral") { has_neutral = true; break; }
  if (!has_neutral) return false;
  table() = std::move(v);
  return true;
}

}  // namespace buddy
