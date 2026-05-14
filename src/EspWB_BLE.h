#pragma once
#include <Arduino.h>
#include <functional>

// MARK: BLE - Bluedroid (arduino-esp32 core).
//   broadcast(data) updates an always-on advertising payload that peer ESP32s
//   pick up via onReceive(). The same advertising also exposes a NUS GATT
//   server so a browser (Web Bluetooth) can connect and write messages
//   (-> onMessage), or watch peer broadcasts relayed as "<mac> <payload>"
//   lines on the NUS TX notify char.
class BLE {
public:
  using OnRx  = std::function<void(const uint8_t mac[6],
                                   const uint8_t* data, size_t len,
                                   int8_t rssi)>;
  using OnMsg = OnRx;            // browser message: (its MAC = zeros, data, len, 0)

  String begin();                // start adv + scan + GATT, returns own MAC ("" on fail)
  void   end();

  bool broadcast(const uint8_t* data, size_t len);   // payload <= maxBroadcastLen()
  bool broadcast(const String& s);

  void onReceive(OnRx cb);       // a broadcast caught from a peer
  void onMessage(OnMsg cb);      // message written by a connected browser

  bool isConnected() const;      // a browser is connected over BLE

  static constexpr size_t maxBroadcastLen() { return 18; }  // one legacy adv packet

  static void getOwnMac(uint8_t out[6]);             // valid after begin()
  static void printMac(Print& p, const uint8_t mac[6]);
};
