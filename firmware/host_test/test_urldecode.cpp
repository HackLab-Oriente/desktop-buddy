// Host-side unit test for the urldecode utility. No ESP-IDF required:
//   c++ -std=c++17 -I../components/webui/include test_urldecode.cpp && ./a.out
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>

#include "urldecode.h"

using buddy::urldecode;

void test(const char* input, size_t max_len, const char* expected) {
  char buf[256];
  memset(buf, 'X', sizeof(buf)); // Fill with sentinel
  
  urldecode(buf, max_len, input);
  
  std::string result(buf);
  if (result != expected) {
    fprintf(stderr, "FAIL: input='%s', max_len=%zu\n", input, max_len);
    fprintf(stderr, "  Expected: '%s'\n", expected);
    fprintf(stderr, "  Got:      '%s'\n", result.c_str());
    assert(false);
  }
}

int main() {
  printf("Running urldecode tests...\n");

  // 1. Basic decode
  test("hello%20world", 64, "hello world");
  
  // 2. Plus sign decoding
  test("hello+world", 64, "hello world");
  
  // 3. Lowercase hex decoding (which had the bug)
  test("%2c%21%3f", 64, ",!?");
  test("%6f%6b", 64, "ok"); // 'o' is 6f, 'k' is 6b
  test("%6F%6B", 64, "ok"); // Uppercase hex
  
  // 4. Boundary cases / truncation
  test("12345", 3, "12"); // Truncates, leaving space for \0
  test("12345", 6, "12345");
  test("12345", 5, "1234");
  
  // 5. Overflow protection (the major security fix)
  test("A%20B%20C%20D", 6, "A B C"); // Will be truncated to 5 chars + \0
  
  // 6. Invalid percent encoding (should just copy literally if incomplete or invalid)
  test("hello%", 64, "hello%");
  test("hello%2", 64, "hello%2");
  test("hello%2z", 64, "hello%2z");
  
  printf("urldecode: all tests passed\n");
  return 0;
}
