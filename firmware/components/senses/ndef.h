#pragma once
// NDEF decoding for Type 2 tags (NTAG21x), kept free of ESP-IDF headers so it
// can be tested on a laptop — see host_test/test_ndef.cpp. It is pure byte
// arithmetic over untrusted input from a sticker anyone can hand you, which is
// exactly the kind of code that should not first run on a microcontroller.
#include <cstdint>
#include <cstring>

namespace buddy {
namespace ndef {

// The handful of URI prefixes worth expanding; anything else comes through as
// the raw remainder, which is still more useful than a leading byte of enum.
inline const char* uri_prefix(uint8_t code) {
  switch (code) {
    case 0x01: return "http://www.";
    case 0x02: return "https://www.";
    case 0x03: return "http://";
    case 0x04: return "https://";
    default:   return "";
  }
}

// Decode the first NDEF record in a Type 2 tag's user memory into `out`.
// Handles Well Known Text and URI records, which is what a phone writes.
// Returns the string length, or 0 when there is nothing readable.
//
// Every length in here came off the tag, so every one is treated as a lie
// until clamped against what was actually read.
inline int first_record(const uint8_t* d, int n, char* out, int out_max) {
  if (!d || !out || out_max < 1) return 0;
  int i = 0;
  while (i < n) {                            // walk the TLV chain
    const uint8_t t = d[i];
    if (t == 0x00) { i++; continue; }        // NULL TLV, padding
    if (t == 0xFE) return 0;                 // terminator
    if (i + 1 >= n) return 0;
    int len = d[i + 1], hdr = 2;
    if (len == 0xFF) {                       // 3-byte length form
      if (i + 3 >= n) return 0;
      len = (d[i + 2] << 8) | d[i + 3];
      hdr = 4;
    }
    if (t != 0x03) { i += hdr + len; continue; }   // not the NDEF TLV

    const uint8_t* m = d + i + hdr;
    int avail = n - (i + hdr);
    if (len < avail) avail = len;
    if (avail < 4) return 0;

    const uint8_t flags = m[0];
    const uint8_t tnf = flags & 0x07;
    const uint8_t type_len = m[1];
    int p = 2, payload_len;
    if (flags & 0x10) {                      // SR: 1-byte payload length
      payload_len = m[p]; p += 1;
    } else {
      if (p + 4 > avail) return 0;
      payload_len = (m[p] << 24) | (m[p+1] << 16) | (m[p+2] << 8) | m[p+3];
      p += 4;
    }
    if (payload_len < 0) return 0;
    int id_len = 0;
    if (flags & 0x08) {                      // IL
      if (p >= avail) return 0;
      id_len = m[p]; p += 1;
    }
    if (p + type_len + id_len > avail) return 0;
    const uint8_t* type = m + p;
    p += type_len + id_len;
    // Clamp by subtraction, never by `p + payload_len > avail`: payload_len is
    // a 32-bit number chosen by whoever wrote the tag, and 0x7FFFFFFF + p
    // overflows to negative, sails past the check and hands memcpy a length
    // longer than the buffer. `avail - p` cannot overflow — p <= avail here.
    if (payload_len > avail - p) payload_len = avail - p;   // truncated read
    if (payload_len <= 0 || tnf != 0x01 || type_len != 1) return 0;
    const uint8_t* payload = m + p;

    if (type[0] == 'T') {                    // status byte + language + text
      const int lang = payload[0] & 0x3F;
      int tl = payload_len - 1 - lang;
      if (tl <= 0) return 0;
      if (tl > out_max - 1) tl = out_max - 1;
      memcpy(out, payload + 1 + lang, tl);
      out[tl] = '\0';
      return tl;
    }
    if (type[0] == 'U') {                    // prefix code + remainder
      const char* pre = uri_prefix(payload[0]);
      int pl = static_cast<int>(strlen(pre));
      int rl = payload_len - 1;
      if (rl < 0) return 0;
      if (pl > out_max - 1) pl = out_max - 1;
      if (pl + rl > out_max - 1) rl = out_max - 1 - pl;
      memcpy(out, pre, pl);
      memcpy(out + pl, payload + 1, rl);
      out[pl + rl] = '\0';
      return pl + rl;
    }
    return 0;
  }
  return 0;
}

}  // namespace ndef
}  // namespace buddy
