#pragma once

#include <stdint.h>
#include <stddef.h>

namespace buddy {

// Rewrites a DNS query buffer into a DNS response that resolves all A record 
// queries to 192.168.4.1. Used for the captive portal.
// Returns the length of the new response packet, or 0 if the query is invalid
// or buffer is too small.
inline size_t process_dns_query(uint8_t* buf, size_t len, size_t max_len) {
    if (len < 12) return 0;
    if ((buf[2] & 0x80) != 0) return 0; // Not a query

    buf[2] |= 0x80;               // QR: this is a response
    buf[3] = 0x00;                // RA/AD/CD off, RCODE 0: the query's flags are not ours to echo
    buf[6] = 0; buf[7] = 1;       // ANCOUNT = 1
    buf[8] = 0; buf[9] = 0;       // NSCOUNT = 0
    buf[10] = 0; buf[11] = 0;     // ARCOUNT = 0: the OPT record after the question is not copied

    size_t ptr = 12;
    while (ptr < len && buf[ptr] != 0) {
      uint8_t label_len = buf[ptr];
      if (label_len > 63) return 0; // Invalid label length, prevents infinite loop / malformed
      ptr += label_len + 1;
    }
    ptr += 5; // Skip the null byte, QTYPE, and QCLASS

    // Ensure we have enough space for the answer (16 bytes)
    if (ptr > len || ptr + 16 > max_len) return 0;

    buf[ptr++] = 0xC0; // Pointer to question (always at offset 12 = 0x0C)
    buf[ptr++] = 0x0C;
    buf[ptr++] = 0x00; // Type A
    buf[ptr++] = 0x01;
    buf[ptr++] = 0x00; // Class IN
    buf[ptr++] = 0x01;
    buf[ptr++] = 0x00; // TTL 60
    buf[ptr++] = 0x00;
    buf[ptr++] = 0x00;
    buf[ptr++] = 0x3C;
    buf[ptr++] = 0x00; // RDLENGTH 4
    buf[ptr++] = 0x04;
    buf[ptr++] = 192;  // IP: 192.168.4.1
    buf[ptr++] = 168;
    buf[ptr++] = 4;
    buf[ptr++] = 1;

    return ptr;
}

} // namespace buddy
