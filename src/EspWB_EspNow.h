#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi_types.h>
#include <functional>

// MARK: EspNow - multi-peer ESP-NOW: send/broadcast, callback or polled RX, RSSI.
//
// Runs alongside Wi-Fi STA + BLE. If Wi-Fi STA is already connected when
// begin() is called, ESP-NOW will use the STA's current channel (the
// `channel` argument is ignored to avoid dropping the AP).
class EspNow {
public:
  using OnRecv = std::function<void(const uint8_t mac[6],
                                    const uint8_t* data, size_t len,
                                    int8_t rssi)>;

  // begin() brings up the Wi-Fi MAC in the requested interface (STA by
  // default), pins the channel (default 6) UNLESS Wi-Fi STA is already
  // connected (then it keeps the STA's channel), and registers ESP-NOW
  // callbacks. longRange=true enables ESP-NOW LR (keeps B|G|N).
  bool begin(uint8_t channel = 6,
             wifi_interface_t iface = WIFI_IF_STA,
             bool longRange = false);
  void end();

  bool addPeer(const uint8_t mac[6], const uint8_t* lmk16 = nullptr);  // lmk16=null => no encryption
  bool addBroadcastPeer();
  bool removePeer(const uint8_t mac[6]);

  static bool setPmk(const uint8_t pmk16[16]);     // 16-byte key shared by encrypted peers

  bool send(const uint8_t mac[6], const uint8_t* data, size_t len);
  bool send(const uint8_t mac[6], const String& s);
  bool broadcast(const uint8_t* data, size_t len);
  bool broadcast(const String& s);

  // Polled fallback used when no onReceive() callback is registered.
  size_t available() const;
  bool   read(uint8_t mac[6], uint8_t* buf, size_t bufLen,
              size_t* outLen, int8_t* outRssi);

  void onReceive(OnRecv cb);

  int8_t lastRssi() const;

  static void getOwnMac(uint8_t out[6], wifi_interface_t iface = WIFI_IF_STA);
  static void printMac(Print& p, const uint8_t mac[6]);

  static EspNow* instance();
};
