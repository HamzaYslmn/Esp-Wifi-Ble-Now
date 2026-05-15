#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi_types.h>
#include <functional>

// MARK: EspNow - multi-peer ESP-NOW: send/broadcast, callback or polled RX, RSSI.
// LR PHY + LR-rate broadcast (LORA_250K) always on (modulation only, no extra heat).
// Pass longRange=true to begin() to also boost TX power to +21 dBm (more range,
// warms chip under continuous TX). Channel selection is automatic:
//   - Wi-Fi STA connected: adopts the STA channel (router dictates it).
//   - Wi-Fi STA down: scans channels 1..13, locks to the first one that
//     receives an ESP-NOW frame and stays there until reboot.
// Broadcasts a 1-byte discovery ping at 1 Hz (driven by loop()); the byte
// encodes the sender's channel as 0x80 | channel, so scanning peers re-pin
// to the embedded channel instead of trusting the radio channel they happen
// to be on. Pings are filtered out of onReceive(). Payloads in 0x81..0x8D
// of length 1 are reserved.
class EspNow {
public:
  using OnRecv   = std::function<void(const uint8_t mac[6],
                                      const uint8_t* data, size_t len,
                                      int8_t rssi)>;
  using OnLocked = std::function<void(uint8_t channel)>;

  // channel != 0 pins the radio to that channel (1..13) and disables auto-scan.
  // channel == 0 keeps the auto behavior: adopt STA channel, else hop+lock via pings.
  // LR PHY + LR-rate broadcast (LORA_250K) always on (modulation only, no extra heat).
  // longRange=true also boosts TX power to +21 dBm — gives ~3-4x more range but causes
  // chip warming under continuous TX. Default false = stock ~+18 dBm.
  bool begin(wifi_interface_t iface = WIFI_IF_STA, bool longRange = false, uint8_t channel = 0);
  void end();

  bool addPeer(const uint8_t mac[6], const uint8_t* lmk16 = nullptr);  // lmk16=null => no encryption
  bool addPeers(const uint8_t (*macs)[6], size_t count);                // bulk add (no encryption)
  bool addBroadcastPeer();
  bool removePeer(const uint8_t mac[6]);
  size_t peerCount() const;                                             // excludes broadcast peer

  static bool setPmk(const uint8_t pmk16[16]);     // 16-byte key shared by encrypted peers

  bool send(const uint8_t mac[6], const uint8_t* data, size_t len);
  bool send(const uint8_t mac[6], const String& s);
  bool sendAll(const uint8_t* data, size_t len);                        // unicast to every registered peer (skip broadcast)
  bool sendAll(const String& s);
  bool broadcast(const uint8_t* data, size_t len);     // prepends 1B seq, retransmits N times
  bool broadcast(const String& s);

  // Multi-transmit for broadcast() only (BLE-adv style). n=1 (default) = single send,
  // no overhead. n>1 prepends a 1B rotating seq and re-sends the frame n times with a
  // ~2 ms gap; receivers dedup per source MAC. Keep n small (3-4) — at LR rate each
  // frame is ~10 ms wire time, so n=3 burns ~30 ms / call (~3% duty at 1 Hz).
  void    setRepeats(uint8_t n);
  uint8_t repeats() const;

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
