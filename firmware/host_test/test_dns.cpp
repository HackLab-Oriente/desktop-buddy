#include "../components/webui/include/dns_parser.h"
#include <cassert>
#include <iostream>
#include <vector>
#include <cstring>

using namespace buddy;

void test_standard_query() {
    // Basic A record query for captive.apple.com
    std::vector<uint8_t> pkt = {
        0x12, 0x34, // ID
        0x01, 0x00, // Flags: standard query
        0x00, 0x01, // QDCOUNT = 1
        0x00, 0x00, // ANCOUNT = 0
        0x00, 0x00, // NSCOUNT = 0
        0x00, 0x00, // ARCOUNT = 0
        // QNAME: captive.apple.com
        0x07, 'c','a','p','t','i','v','e',
        0x05, 'a','p','p','l','e',
        0x03, 'c','o','m',
        0x00,       // End of QNAME
        0x00, 0x01, // QTYPE = A
        0x00, 0x01  // QCLASS = IN
    };

    uint8_t buf[512];
    std::memcpy(buf, pkt.data(), pkt.size());

    size_t new_len = process_dns_query(buf, pkt.size(), sizeof(buf));

    assert(new_len > pkt.size()); // Appended response
    assert((buf[2] & 0x80) != 0); // QR is set
    assert(buf[3] == 0x00);       // Flags cleared
    assert(buf[7] == 0x01);       // ANCOUNT = 1
    assert(buf[9] == 0x00);       // NSCOUNT = 0
    assert(buf[11] == 0x00);      // ARCOUNT = 0
    
    // Check IP
    assert(buf[new_len - 4] == 192);
    assert(buf[new_len - 3] == 168);
    assert(buf[new_len - 2] == 4);
    assert(buf[new_len - 1] == 1);
}

void test_edns0_query() {
    // Query with OPT record (ARCOUNT = 1)
    std::vector<uint8_t> pkt = {
        0x56, 0x78, // ID
        0x01, 0x20, // Flags (AD bit set maybe)
        0x00, 0x01, // QDCOUNT = 1
        0x00, 0x00, // ANCOUNT = 0
        0x00, 0x00, // NSCOUNT = 0
        0x00, 0x01, // ARCOUNT = 1
        0x04, 't','e','s','t', 0x00, // QNAME
        0x00, 0x01, // QTYPE = A
        0x00, 0x01, // QCLASS = IN
        // OPT RECORD (We don't parse this but it's part of len)
        0x00, 0x00, 0x29, 0x04, 0xd0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    };

    uint8_t buf[512];
    std::memcpy(buf, pkt.data(), pkt.size());

    size_t new_len = process_dns_query(buf, pkt.size(), sizeof(buf));

    assert(new_len > 0);
    assert(buf[3] == 0x00); // AD cleared
    assert(buf[11] == 0x00); // ARCOUNT forced to 0
}

void test_malformed_label() {
    // Query with a malformed infinite label (length > 63)
    std::vector<uint8_t> pkt = {
        0x12, 0x34, 0x01, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0xC0, // Length 192 (> 63)
        0x00
    };
    uint8_t buf[512];
    std::memcpy(buf, pkt.data(), pkt.size());

    size_t new_len = process_dns_query(buf, pkt.size(), sizeof(buf));
    assert(new_len == 0); // Should safely abort
}

int main() {
    test_standard_query();
    test_edns0_query();
    test_malformed_label();

    std::cout << "All dns_parser tests passed!" << std::endl;
    return 0;
}
