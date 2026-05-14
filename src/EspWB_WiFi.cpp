#include "EspWB_WiFi.h"
#include <esp_wifi.h>

namespace {
// MARK: helpers
// OR the wanted role into the running mode (bitmask: OFF=0 STA=1 AP=2 AP_STA=3).
void ensureMode(wifi_mode_t want) {
  WiFi.mode((wifi_mode_t)(WiFi.getMode() | want));
}

bool waitConnected(uint32_t timeoutMs) {
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - t0) < timeoutMs) {
    delay(50);
  }
  return WiFi.status() == WL_CONNECTED;
}

const char* orNull(const char* pass) {            // null => open network
  return (pass && *pass) ? pass : nullptr;
}
} // namespace

// MARK: bring-up
bool Wifi::beginSTA(const char* ssid, const char* pass, uint32_t timeoutMs) {
  ensureMode(WIFI_STA);
  WiFi.begin(ssid, pass);
  bool ok = waitConnected(timeoutMs);
  // Leave Wi-Fi modem-sleep at the Arduino default (WIFI_PS_MIN_MODEM): per
  // the official ESP-IDF coexistence guide, customising power-save params
  // "leads to extra Wi-Fi priority requests, impacting BT performance" and
  // hurts BLE coexistence. Default behaviour gives the coex module a stable
  // schedule for slot allocation.
  return ok;
}

bool Wifi::beginAP(const char* ssid, const char* pass, uint8_t channel, bool hidden) {
  ensureMode(WIFI_AP);
  return WiFi.softAP(ssid, orNull(pass), channel, hidden ? 1 : 0);
}

bool Wifi::beginAPSTA(const char* apSsid, const char* apPass,
                      const char* staSsid, const char* staPass,
                      uint8_t apChannel, uint32_t staTimeoutMs) {
  WiFi.mode(WIFI_AP_STA);
  bool apOk = WiFi.softAP(apSsid, orNull(apPass), apChannel);
  WiFi.begin(staSsid, staPass);
  waitConnected(staTimeoutMs);
  return apOk;
}

// MARK: info
uint8_t Wifi::channel() {
  uint8_t prim = 0;
  wifi_second_chan_t sec;
  esp_wifi_get_channel(&prim, &sec);
  return prim;
}

IPAddress Wifi::staIP() { return WiFi.localIP(); }
IPAddress Wifi::apIP()  { return WiFi.softAPIP(); }
