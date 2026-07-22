#include "face_model.h"

#include <cctype>
#include <cstring>

namespace buddy {

const Emotion kEmotions[] = {
    {"neutral",    {26, 30, 100, 0,  0},  3800},
    {"happy",      {26, 30, 100, 14, 0},  3000},
    {"curious",    {30, 34, 100, 0,  0},  2600},
    {"sleepy",     {26, 30, 35,  0,  0},  6000},
    {"surprised",  {34, 40, 100, 0,  0},  5000},
    {"angry",      {28, 28, 100, 0,  1},  3200},
    {"sad",        {24, 26, 75,  0, -1},  5200},
    {"suspicious", {26, 30, 55,  0,  1},  2200},
};
const int kEmotionCount = sizeof(kEmotions) / sizeof(kEmotions[0]);

int emotion_index(const char* name) {
  for (int i = 0; i < kEmotionCount; i++)
    if (std::strcmp(name, kEmotions[i].name) == 0) return i;
  return -1;
}

const uint8_t kFont[][5] = {
    {0b010,0b101,0b111,0b101,0b101}, {0b110,0b101,0b110,0b101,0b110},  // A B
    {0b011,0b100,0b100,0b100,0b011}, {0b110,0b101,0b101,0b101,0b110},  // C D
    {0b111,0b100,0b110,0b100,0b111}, {0b111,0b100,0b110,0b100,0b100},  // E F
    {0b011,0b100,0b101,0b101,0b011}, {0b101,0b101,0b111,0b101,0b101},  // G H
    {0b111,0b010,0b010,0b010,0b111}, {0b001,0b001,0b001,0b101,0b010},  // I J
    {0b101,0b110,0b100,0b110,0b101}, {0b100,0b100,0b100,0b100,0b111},  // K L
    {0b101,0b111,0b111,0b101,0b101}, {0b110,0b101,0b101,0b101,0b101},  // M N
    {0b010,0b101,0b101,0b101,0b010}, {0b110,0b101,0b110,0b100,0b100},  // O P
    {0b010,0b101,0b101,0b010,0b001}, {0b110,0b101,0b110,0b110,0b101},  // Q R
    {0b011,0b100,0b010,0b001,0b110}, {0b111,0b010,0b010,0b010,0b010},  // S T
    {0b101,0b101,0b101,0b101,0b111}, {0b101,0b101,0b101,0b101,0b010},  // U V
    {0b101,0b101,0b111,0b111,0b101}, {0b101,0b101,0b010,0b101,0b101},  // W X
    {0b101,0b101,0b010,0b010,0b010}, {0b111,0b001,0b010,0b100,0b111},  // Y Z
    {0b111,0b101,0b101,0b101,0b111}, {0b010,0b110,0b010,0b010,0b111},  // 0 1
    {0b110,0b001,0b010,0b100,0b111}, {0b110,0b001,0b010,0b001,0b110},  // 2 3
    {0b101,0b101,0b111,0b001,0b001}, {0b111,0b100,0b110,0b001,0b110},  // 4 5
    {0b011,0b100,0b110,0b101,0b010}, {0b111,0b001,0b010,0b010,0b010},  // 6 7
    {0b111,0b101,0b111,0b101,0b111}, {0b010,0b101,0b011,0b001,0b110},  // 8 9
    {0b000,0b000,0b000,0b000,0b000}, {0b000,0b000,0b000,0b000,0b010},  // sp .
    {0b000,0b000,0b000,0b010,0b100}, {0b010,0b010,0b010,0b000,0b010},  // , !
    {0b110,0b001,0b010,0b000,0b010}, {0b010,0b010,0b000,0b000,0b000},  // ? '
    {0b000,0b000,0b111,0b000,0b000}, {0b000,0b010,0b000,0b010,0b000},  // - :
};

int glyph_index(char c) {
  c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= '0' && c <= '9') return 26 + (c - '0');
  switch (c) {
    case '.': return 37; case ',': return 38; case '!': return 39;
    case '?': return 40; case '\'': return 41; case '-': return 42;
    case ':': return 43; default: return 36;  // space
  }
}

}  // namespace buddy
