#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi_types.h>
#include <functional>

// MARK: EspNow - multi-peer ESP-NOW: send/broadcast, callback or polled RX, RSSI.
// begin() picks the channel automatically:
//   - Wi-Fi STA connected: adopts the STA channel (router dictates it).
//   - Wi-Fi STA down: scans channels 1..13, locks to the first one that
//     receives an ESP-NOW frame and stays there until reboot.
// Broadcasts a 1-byte discovery ping at 1 Hz (driven by loop()); the byte
// encodes the sender's channel as 0x80 | channel, so scanning peers re-pin
// to the embedded channel instead of trusting the radio channel they happen
// to be on (avoids latching onto adjacent-channel leaks). Pings are filtered
// out of onReceive(). 1-byte payloads in 0x81..0x8D are reserved.
class EspNow {
public:
  using OnRecv   = std::function<void(const uint8_t mac[6],
                                      const uint8_t* data, size_t len,
                                      int8_t rssi)>;
  using OnLocked = std::function<void(uint8_t channel)>;

  // Pins radio to `channel`, registers ESP-NOW callbacks. longRange=true enables LR PHY + max TX.
  // longRange defaults off: LR pegs ESP-NOW to ~250 kbps so each frame occupies the radio ~10x
  // longer, which collides badly with BLE scan on the no-STA peer (ESP-IDF coex marks ESP-NOW RX
  // + BLE Scan as "stable in STA mode, otherwise not supported").
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

  // Polled fallback when no onReceive() callback is registered.
  size_t available() const;
  bool   read(uint8_t mac[6], uint8_t* buf, size_t bufLen,
              size_t* outLen, int8_t* outRssi);

  void onReceive(OnRecv cb);
  void onLocked(OnLocked cb);  // fired once from loop() when scan locks a channel

  void loop();                 // call each loop iteration to drive channel scan

  int8_t  lastRssi() const;
  uint8_t channel()  const;    // current radio channel (hops while scanning)
  bool    isScanning() const;  // true until first ESP-NOW frame locks a channel

  static void getOwnMac(uint8_t out[6], wifi_interface_t iface = WIFI_IF_STA);
  static void printMac(Print& p, const uint8_t mac[6]);

  static EspNow* instance();
};
