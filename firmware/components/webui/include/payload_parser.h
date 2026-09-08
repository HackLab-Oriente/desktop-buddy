#pragma once

#include <string.h>
#include "urldecode.h"

namespace buddy {

struct setup_payload_t {
  char ssid[64];
  char pass[64];
  char auth[16];
};

inline void parse_setup_payload(char* buf, setup_payload_t* out) {
  memset(out, 0, sizeof(setup_payload_t));
  char* p = strtok(buf, "&");
  while (p) {
    char* eq = strchr(p, '=');
    if (eq) {
      *eq = '\0';
      if (strcmp(p, "ssid") == 0) {
        urldecode(out->ssid, sizeof(out->ssid), eq + 1);
      } else if (strcmp(p, "pass") == 0) {
        urldecode(out->pass, sizeof(out->pass), eq + 1);
      } else if (strcmp(p, "auth") == 0) {
        urldecode(out->auth, sizeof(out->auth), eq + 1);
      }
    }
    p = strtok(nullptr, "&");
  }
}

} // namespace buddy
