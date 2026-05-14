#pragma once
#include <Arduino.h>

// MARK: MAC helpers - shared across the library.

// Format a 6-byte MAC as "aa:bb:cc:dd:ee:ff".
inline void espwbnPrintMac(Print& p, const uint8_t mac[6]) {
  char buf[18];
  snprintf(buf, sizeof(buf), "%02x:%02x:%02x:%02x:%02x:%02x",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  p.print(buf);
}

// Parse "aa:bb:cc:dd:ee:ff" into 6 bytes (strtoul stops at each ':').
inline void espwbnParseMac(const char* s, uint8_t out[6]) {
  for (int i = 0; i < 6; ++i) out[i] = (uint8_t)strtoul(s + i * 3, nullptr, 16);
}
