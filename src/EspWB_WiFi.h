#pragma once
#include <Arduino.h>
#include <WiFi.h>

// MARK: Wifi - STA / AP / AP+STA bring-up helpers.
class Wifi {
public:
  // longRange=true cranks TX power to +20 dBm (PHY unchanged, affects ESP-NOW too).
  static bool beginSTA(const char* ssid, const char* pass, uint32_t timeoutMs = 15000,
                       bool longRange = false);

  static bool beginAP(const char* ssid, const char* pass = nullptr,
                      uint8_t channel = 1, bool hidden = false,
                      bool longRange = false);                          // pass=null/"" => open

  // AP + STA together; AP follows router channel once STA associates.
  static bool beginAPSTA(const char* apSsid, const char* apPass,
                         const char* staSsid, const char* staPass,
                         uint8_t apChannel = 1, uint32_t staTimeoutMs = 15000,
                         bool longRange = false);

  static uint8_t channel();      // current primary Wi-Fi channel
  static IPAddress staIP();
  static IPAddress apIP();
};
