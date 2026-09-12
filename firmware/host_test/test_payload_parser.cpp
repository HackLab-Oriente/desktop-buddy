#include "payload_parser.h"
#include <cassert>
#include <iostream>
#include <string.h>

void test_parse(const char* input, const char* exp_ssid, const char* exp_pass, const char* exp_auth) {
  char buf[256];
  strncpy(buf, input, sizeof(buf) - 1);
  buf[sizeof(buf) - 1] = '\0';
  
  buddy::setup_payload_t out;
  buddy::parse_setup_payload(buf, &out);
  
  if (strcmp(out.ssid, exp_ssid) != 0) {
    std::cerr << "SSID mismatch. Expected: " << exp_ssid << ", Got: " << out.ssid << std::endl;
    assert(false);
  }
  
  if (strcmp(out.pass, exp_pass) != 0) {
    std::cerr << "Pass mismatch. Expected: " << exp_pass << ", Got: " << out.pass << std::endl;
    assert(false);
  }

  if (strcmp(out.auth, exp_auth) != 0) {
    std::cerr << "Auth mismatch. Expected: " << exp_auth << ", Got: " << out.auth << std::endl;
    assert(false);
  }
}

int main() {
  // Test basic parsing
  test_parse("ssid=RANDOM_SSID&pass=secret&auth=5", "RANDOM_SSID", "secret", "5");
  
  // Test missing fields
  test_parse("ssid=OpenNet", "OpenNet", "", "");
  test_parse("pass=secret", "", "secret", "");
  
  // Test URL encoding with special characters
  test_parse("ssid=My%20Net&pass=My%26Pass%3Dword&auth=3", "My Net", "My&Pass=word", "3");
  
  // Test out-of-order fields
  test_parse("auth=0&ssid=Test&pass=123", "Test", "123", "0");
  
  // Test truncation on very long string (max size is 64 for pass)
  char long_payload[256];
  snprintf(long_payload, sizeof(long_payload), "ssid=Test&pass=%s", "1234567890123456789012345678901234567890123456789012345678901234567890"); // 70 characters
  // Expected to truncate at 63 characters + null
  test_parse(long_payload, "Test", "123456789012345678901234567890123456789012345678901234567890123", "");
  
  std::cout << "All payload parser tests passed!" << std::endl;
  return 0;
}
