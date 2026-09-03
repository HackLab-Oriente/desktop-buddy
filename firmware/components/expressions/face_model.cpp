#include "face_model.h"

#include <cstring>
#include <utility>

namespace buddy {
namespace {

// The fallback set. A board with no pack — or with a pack that fails to
// parse — still has a face. See set_emotions() for why "neutral" is required.
const std::vector<Emotion>& builtins() {
  static const std::vector<Emotion> v = {
      //name          eye {w,h,open,lift,brow}   blink  r    g    b     mood
      {"neutral",    {26, 30, 100,  0,  0},      3800,   0, 190, 255, "calm"},
      {"happy",      {26, 30, 100, 14,  0},      3000,  40, 235, 120, "excited"},
      {"curious",    {30, 34, 100,  0,  0},      2600,   0, 210, 235, "thinking"},
      {"sleepy",     {26, 30,  35,  0,  0},      6000,  90,  90, 200, "off"},
      {"surprised",  {34, 40, 100,  0,  0},      5000, 150, 240, 255, "excited"},
      {"angry",      {28, 28, 100,  0,  1},      3200, 255,  50,  25, "calm"},
      {"sad",        {24, 26,  75,  0, -1},      5200,  50, 110, 255, "calm"},
      {"suspicious", {26, 30,  55,  0,  1},      2200, 255, 180,  20, "calm"},
  };
  return v;
}

std::vector<Emotion>& table() {
  static std::vector<Emotion> t = builtins();
  return t;
}

// Set once the render task exists. From then on the table's storage is being
// read every frame and must not move.
bool s_frozen = false;

}  // namespace

const Emotion* emotions() { return table().data(); }
int emotion_count() { return static_cast<int>(table().size()); }

int emotion_index(const char* name) {
  const std::vector<Emotion>& t = table();
  for (size_t i = 0; i < t.size(); i++)
    if (t[i].name == name) return static_cast<int>(i);
  return -1;
}

void freeze_emotions() { s_frozen = true; }

bool set_emotions(std::vector<Emotion> v) {
  if (s_frozen) return false;   // a swap here would free the table mid-frame
  if (v.empty()) return false;
  bool has_neutral = false;
  for (const Emotion& e : v)
    if (e.name == "neutral") { has_neutral = true; break; }
  if (!has_neutral) return false;
  table() = std::move(v);
  return true;
}

}  // namespace buddy
