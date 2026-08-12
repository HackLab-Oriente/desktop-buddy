#pragma once
// Folding arbitrary UTF-8 down to what the face can actually draw.
//
// Kept free of ESP-IDF headers so it can be tested on a laptop -- see
// host_test/test_latin1.cpp. It walks bytes that came from an LLM into a fixed
// buffer, which is the same shape of code as the NDEF decoder, and that one
// shipped with an out-of-bounds read until a host test found it.
#include <cstddef>
#include <cstdint>

namespace buddy {
namespace latin1 {

// FontLatin covers U+0020-U+00FF, which is every character Spanish needs. But
// an LLM reaches past that constantly — curly quotes, em dashes, ellipses —
// and a codepoint the font lacks draws as nothing at all, so "don't" silently
// loses its apostrophe. Fold the usual suspects down to ASCII and drop the
// rest, once on arrival rather than once per band.
inline void copy_display_text(char* dst, size_t dst_size, const char* src) {
  size_t o = 0;
  auto put = [&](const char* s) { while (*s && o + 1 < dst_size) dst[o++] = *s++; };
  while (*src && o + 1 < dst_size) {
    const unsigned char c = static_cast<unsigned char>(*src);
    uint32_t cp;
    int len;
    if (c < 0x80)                { cp = c;        len = 1; }
    else if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; len = 2; }
    else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; len = 3; }
    else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; len = 4; }
    else { src++; continue; }                     // stray continuation byte
    for (int i = 1; i < len; i++) {
      if ((src[i] & 0xC0) != 0x80) { cp = 0; len = i; break; }  // truncated
      cp = (cp << 6) | (src[i] & 0x3F);
    }
    src += len;
    if (cp == 0) continue;

    switch (cp) {
      case 0x2018: case 0x2019: case 0x02BC: put("'");   continue;  // ' '
      case 0x201C: case 0x201D:              put("\"");  continue;  // " "
      case 0x2013: case 0x2014:              put("-");   continue;  // – —
      case 0x2026:                           put("...");  continue;  // …
      case 0x00A0:                           put(" ");   continue;  // nbsp
      default: break;
    }
    if (cp < 0x80) {
      dst[o++] = static_cast<char>(cp);
    } else if (cp <= 0xFF) {                      // back to UTF-8 for LovyanGFX
      if (o + 2 >= dst_size) break;
      dst[o++] = static_cast<char>(0xC0 | (cp >> 6));
      dst[o++] = static_cast<char>(0x80 | (cp & 0x3F));
    }
    // Anything above U+00FF is dropped: the font has no glyph, and a silent
    // gap reads better than a box.
  }
  dst[o] = '\0';
}

}  // namespace latin1
}  // namespace buddy
