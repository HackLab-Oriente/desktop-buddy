// Host test for the display text normaliser.
//   c++ -std=c++17 -Wall -I../components/expressions test_latin1.cpp -o test_latin1
//
// The face can draw U+0020-U+00FF. This folds everything else down to that,
// and it walks LLM output byte by byte into a fixed buffer — so the malformed
// cases below matter more than the tidy ones.
#include "latin1.h"

#include <cstdio>
#include <cstring>
#include <string>

static int failures = 0;

static void check(const char* what, const std::string& in, const std::string& want,
                  size_t bufsize = 128) {
  char buf[256];
  memset(buf, 0x7F, sizeof buf);
  buddy::latin1::copy_display_text(buf, bufsize, in.c_str());
  const std::string got(buf);
  if (got != want) {
    printf("  FAIL %-36s want \"%s\", got \"%s\"\n", what, want.c_str(), got.c_str());
    failures++;
  } else {
    printf("  ok   %-36s \"%s\"\n", what, got.c_str());
  }
  if (strlen(buf) >= bufsize) { printf("  FAIL %-36s overran buffer\n", what); failures++; }
}

int main() {
  printf("latin1: spanish survives intact\n");
  check("plain ascii", "hola equipo", "hola equipo");
  check("accents pass through", "cómo estás", "cómo estás");
  check("enye", "mañana", "mañana");
  check("inverted marks", "¿qué tal? ¡genial!", "¿qué tal? ¡genial!");
  check("diaeresis", "pingüino", "pingüino");
  check("uppercase accents", "ÁÉÍÓÚÑ", "ÁÉÍÓÚÑ");

  printf("latin1: what an LLM actually emits\n");
  check("curly apostrophe", "don’t", "don't");
  check("curly quotes", "dijo “hola”", "dijo \"hola\"");
  check("em dash", "sí — claro", "sí - claro");
  check("en dash", "1–2", "1-2");
  check("ellipsis", "pensando…", "pensando...");
  check("non-breaking space", "a b", "a b");

  printf("latin1: out of range is dropped, not boxed\n");
  check("emoji dropped", "hola \U0001F600 mundo", "hola  mundo");
  check("cjk dropped", "a中文b", "ab");
  check("arrow dropped", "a→b", "ab");

  printf("latin1: malformed input\n");
  check("stray continuation byte", std::string("a\x80\x81") + "b", "ab");
  check("truncated 2-byte seq", std::string("a\xC3"), "a");
  check("truncated 3-byte seq", std::string("a\xE2\x80"), "a");
  check("lone lead byte then ascii", std::string("a\xC3z"), "az");
  check("empty", "", "");

  printf("latin1: bounds\n");
  check("truncates to buffer", "abcdefghij", "abcd", 5);
  // 'á' is two bytes: with four bytes of room only one fits, and the second
  // must not be half-written — a split sequence would confuse the decoder.
  check("no split sequence at edge", "ááá", "á", 4);
  check("buffer of one", "hola", "", 1);

  if (failures) { printf("\nlatin1: %d FAILED\n", failures); return 1; }
  printf("\nlatin1: all tests passed\n");
  return 0;
}
