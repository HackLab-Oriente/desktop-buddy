#include "face_model.h"

#include <cstring>

namespace buddy {

const Emotion kEmotions[] = {
    //  name         eye geometry            blink   mood color (r,g,b) — distinct per mood
    {"neutral",    {26, 30, 100, 0,  0},  3800,   0, 190, 255},  // cyan
    {"happy",      {26, 30, 100, 14, 0},  3000,  40, 235, 120},  // green
    {"curious",    {30, 34, 100, 0,  0},  2600,   0, 210, 235},  // teal
    {"sleepy",     {26, 30, 35,  0,  0},  6000,  90,  90, 200},  // dim indigo
    {"surprised",  {34, 40, 100, 0,  0},  5000, 150, 240, 255},  // pale white-cyan
    {"angry",      {28, 28, 100, 0,  1},  3200, 255,  50,  25},  // red
    {"sad",        {24, 26, 75,  0, -1},  5200,  50, 110, 255},  // blue
    {"suspicious", {26, 30, 55,  0,  1},  2200, 255, 180,  20},  // amber
};
const int kEmotionCount = sizeof(kEmotions) / sizeof(kEmotions[0]);

int emotion_index(const char* name) {
  for (int i = 0; i < kEmotionCount; i++)
    if (std::strcmp(name, kEmotions[i].name) == 0) return i;
  return -1;
}

}  // namespace buddy
