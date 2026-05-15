#pragma once
#include <Arduino.h>
#include <functional>

// MARK: BLE - Bluedroid mesh broadcast + NUS GATT for browser connect.
class BLE {
public:
  using OnRx  = std::function<void(const uint8_t mac[6],
                                   const uint8_t* data, size_t len,
                                   int8_t rssi)>;
  using OnMsg = OnRx;            // browser message: (its MAC = zeros, data, len, 0)

  // longRange=true bumps TX power to +9 dBm (PHY stays 1M for BLE 4 compat).
  String begin(bool longRange = false);  // start adv + scan + GATT, returns own MAC ("" on fail)
  void   end();

  bool broadcast(const uint8_t* data, size_t len);   // payload <= maxBroadcastLen()
  bool broadcast(const String& s);

  void onReceive(OnRx cb);       // a broadcast caught from a peer
  void onMessage(OnMsg cb);      // message written by a connected browser

  bool isConnected() const;      // a browser is connected over BLE

  static constexpr size_t maxBroadcastLen() { return 18; }  // one legacy adv packet

  // Custom TX power? Just call BLEDevice::setPower(lvl) directly (native).
  static void getOwnMac(uint8_t out[6]);             // valid after begin()
  static void printMac(Print& p, const uint8_t mac[6]);
};
