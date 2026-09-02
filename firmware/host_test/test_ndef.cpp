// Host test for the NDEF decoder. Runs on a laptop, no board required:
//   c++ -std=c++17 -Wall -I../components/senses test_ndef.cpp -o test_ndef
//
// Worth having because every length in an NDEF message comes off the tag, and
// a tag is an object a stranger can leave on your desk. The malformed cases at
// the bottom are the point of this file; the happy paths are the easy half.
#include "ndef.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static int failures = 0;

static void check(const char* what, const std::vector<uint8_t>& tag,
                  const char* expected, int out_max = 128) {
  char out[256];
  memset(out, 0xAA, sizeof out);
  const int n = buddy::ndef::first_record(tag.data(), static_cast<int>(tag.size()),
                                          out, out_max);
  const std::string got = (n > 0) ? std::string(out, n) : std::string();
  const bool ok = got == expected && n == static_cast<int>(strlen(expected));
  if (!ok) {
    printf("  FAIL %-38s expected \"%s\", got \"%s\" (n=%d)\n", what, expected,
           got.c_str(), n);
    failures++;
  } else {
    printf("  ok   %-38s \"%s\"\n", what, got.c_str());
  }
}

// Wrap an NDEF message in the Type 2 TLV container a phone writes.
static std::vector<uint8_t> tlv(const std::vector<uint8_t>& msg) {
  std::vector<uint8_t> v{0x03, static_cast<uint8_t>(msg.size())};
  v.insert(v.end(), msg.begin(), msg.end());
  v.push_back(0xFE);
  return v;
}

static std::vector<uint8_t> text_record(const std::string& s,
                                        const std::string& lang = "en") {
  std::vector<uint8_t> m{0xD1, 0x01,
                         static_cast<uint8_t>(1 + lang.size() + s.size()), 'T',
                         static_cast<uint8_t>(lang.size())};
  m.insert(m.end(), lang.begin(), lang.end());
  m.insert(m.end(), s.begin(), s.end());
  return m;
}

static std::vector<uint8_t> uri_record(uint8_t prefix, const std::string& rest) {
  std::vector<uint8_t> m{0xD1, 0x01, static_cast<uint8_t>(1 + rest.size()), 'U',
                         prefix};
  m.insert(m.end(), rest.begin(), rest.end());
  return m;
}

int main() {
  printf("ndef: what a phone actually writes\n");
  check("text record", tlv(text_record("mood:happy")), "mood:happy");
  check("text, spanish lang code", tlv(text_record("pack:pirata", "es")), "pack:pirata");
  check("text, empty lang code", tlv(text_record("hola", "")), "hola");
  check("uri https://", tlv(uri_record(0x04, "hacklaboriente.org")),
        "https://hacklaboriente.org");
  check("uri http://www.", tlv(uri_record(0x01, "example.com")),
        "http://www.example.com");
  check("uri no prefix", tlv(uri_record(0x00, "urn:x:1")), "urn:x:1");

  printf("ndef: containers\n");
  {  // NULL TLVs are legal padding before the message
    auto v = tlv(text_record("pad"));
    v.insert(v.begin(), {0x00, 0x00});
    check("leading NULL TLVs", v, "pad");
  }
  {  // an unrelated TLV (lock control) must be skipped, not parsed
    auto v = tlv(text_record("skip"));
    v.insert(v.begin(), {0x01, 0x03, 0xAA, 0xBB, 0xCC});
    check("unknown TLV skipped", v, "skip");
  }
  {  // record with an ID field
    auto m = text_record("withid");
    m[0] |= 0x08;                                   // IL
    m.insert(m.begin() + 3, 0x02);                  // id length
    m.insert(m.begin() + 5, {0x41, 0x42});          // id bytes, after type
    check("ID field present", tlv(m), "withid");
  }

  printf("ndef: nothing to say\n");
  check("blank tag (all zero)", std::vector<uint8_t>(32, 0x00), "");
  check("terminator first", {0xFE, 0x00, 0x00}, "");
  check("empty input", {}, "");
  check("text record with no text", tlv(text_record("")), "");

  printf("ndef: malformed and hostile\n");
  {  // claims 200 bytes of payload inside a 20-byte read
    auto m = text_record("short");
    m[2] = 200;
    check("payload length overruns buffer", tlv(m), "short");
  }
  {  // TLV length longer than the data actually present
    auto v = tlv(text_record("clip"));
    v[1] = 0xF0;
    check("TLV length overruns buffer", v, "clip");
  }
  {  // truncated mid-text: 12 bytes is the header plus three characters
    auto v = tlv(text_record("truncated here"));
    v.resize(12);
    check("truncated mid-text", v, "tru");
  }
  {  // truncated before any text at all — only a partial language code left
    auto v = tlv(text_record("truncated here"));
    v.resize(8);
    check("truncated before text", v, "");
  }
  check("TLV header cut off", {0x03}, "");
  {  // non-well-known TNF (MIME) must be ignored, not misread as text
    auto m = text_record("nope");
    m[0] = (m[0] & 0xF8) | 0x02;                    // TNF = MIME
    check("TNF media type ignored", tlv(m), "");
  }
  {  // language length byte larger than the payload
    auto m = text_record("x");
    m[4] = 0x3F;
    check("lang length overruns payload", tlv(m), "");
  }
  {  // 4-byte length form with an absurd value
    auto m = text_record("abc");
    m[0] &= ~0x10;                                  // clear SR
    m.erase(m.begin() + 2);
    m.insert(m.begin() + 2, {0x7F, 0xFF, 0xFF, 0xFF});
    check("non-short record, huge length", tlv(m), "abc");
  }

  printf("ndef: encoding\n");
  {  // UTF-16 (status bit 7). Copied as UTF-8 this published an empty string,
     // which the event registry says cannot happen.
    std::vector<uint8_t> m{0xD1, 0x01, 0x07, 'T', 0x82, 'e', 'n',
                           0x00, 'h', 0x00, 'i'};
    check("utf-16 text is no content", tlv(m), "");
    std::vector<uint8_t> b{0xD1, 0x01, 0x09, 'T', 0x82, 'e', 'n',
                           0xFE, 0xFF, 0x00, 'h', 0x00, 'i'};
    check("utf-16 with BOM is no content", tlv(b), "");
  }
  {  // an embedded NUL made the returned length disagree with the string
    std::vector<uint8_t> m{0xD1, 0x01, 0x08, 'T', 0x02, 'e', 'n',
                           'a', 'b', 0x00, 'c'};
    check("embedded NUL is no content", tlv(m), "");
  }
  {  // the read window cuts at a byte count, and the content is Spanish
    std::string body(54, 'a');
    body += "\xc3";                                 // lone lead byte of 'ñ'
    check("split utf-8 is trimmed", tlv(text_record(body)), std::string(54, 'a').c_str());
    std::string whole(54, 'a');
    whole += "\xc3\xb1";                            // complete 'ñ'
    check("complete utf-8 survives", tlv(text_record(whole)), whole.c_str());
    check("accents pass through", tlv(text_record("ñoño áé")), "ñoño áé");
  }

  printf("ndef: output clamping\n");
  check("out_max clamps text", tlv(text_record("mood:happy")), "mood", 5);
  check("out_max clamps uri prefix", tlv(uri_record(0x04, "example.com")), "http", 5);
  check("out_max of 1 yields nothing", tlv(text_record("hi")), "", 1);

  if (failures) { printf("\nndef: %d FAILED\n", failures); return 1; }
  printf("\nndef: all tests passed\n");
  return 0;
}
