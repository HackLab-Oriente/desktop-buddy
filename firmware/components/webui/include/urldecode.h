#pragma once

#include <stddef.h>
#include <ctype.h>

namespace buddy {

inline void urldecode(char* dst, size_t max_len, const char* src) {
  char a, b;
  size_t written = 0;
  while (*src && written < max_len - 1) {
    if ((*src == '%') && ((a = src[1]) && (b = src[2])) && (isxdigit(a) && isxdigit(b))) {
      if (a >= 'a') a -= 'a' - 10;
      else if (a >= 'A') a -= 'A' - 10;
      else a -= '0';
      
      if (b >= 'a') b -= 'a' - 10;
      else if (b >= 'A') b -= 'A' - 10;
      else b -= '0';
      
      *dst++ = 16 * a + b;
      src += 3;
    } else if (*src == '+') {
      *dst++ = ' ';
      src++;
    } else {
      *dst++ = *src++;
    }
    written++;
  }
  *dst = '\0';
}

}  // namespace buddy
