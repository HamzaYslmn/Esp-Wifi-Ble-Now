// EspNowAndBleNoWifi - ESP-NOW + BLE coexisting on the same ESP32, no Wi-Fi.
// Both peers must hard-code the SAME channel via now.lockChannel(CH).
// Flash this same sketch on every node.

#include <EspWiFiBLENow.h>

constexpr uint8_t ESPNOW_CHANNEL = 6;     // pick any 1..13, identical on every node

EspNow now;
BLE    ble;

void setup() {
  Serial.begin(115200);
  delay(200);

  // ESP-NOW: STA up but never associates. longRange=true => +21 dBm TX cap.
  // Passing a non-zero channel pins it and disables auto-scan.
  if (!now.begin(WIFI_IF_STA, /*longRange=*/true, ESPNOW_CHANNEL)) {
    Serial.println("ESP-NOW init failed"); while (true) delay(1000);
  }
  now.setRepeats(3);   // BLE-style triple-broadcast (~30 ms airtime / call)

  // // ---- UNICAST MODE ----
  // // Send to a known MAC list instead of broadcasting. No seq prefix, no
  // // multi-tx; ESP-NOW L2 acks each unicast frame.
  // static const uint8_t peers[][6] = {
  //   {0xC0,0x49,0xEF,0xD0,0x3F,0xE0},
  //   {0xC0,0x49,0xEF,0xD4,0x54,0x44},
  // };
  // now.addPeers(peers, sizeof(peers) / 6);

  now.onReceive([](const uint8_t mac[6], const uint8_t* d, size_t n, int8_t rssi) {
    Serial.printf("[NOW %ddBm] ", rssi);
    EspNow::printMac(Serial, mac);
    Serial.printf(" : %.*s\n", (int)n, d);
  });

  // BLE: longRange=true => +9 dBm TX. Phone/browser still see it (no Coded PHY).
  String mac = ble.begin(/*longRange=*/true);
  Serial.printf("BLE up, own MAC %s\n", mac.c_str());
  ble.onReceive([](const uint8_t mac[6], const uint8_t* d, size_t n, int8_t rssi) {
    Serial.printf("[BLE %ddBm] ", rssi);
    BLE::printMac(Serial, mac);
    Serial.printf(" : %.*s\n", (int)n, d);
  });

  Serial.printf("Locked ESP-NOW ch %u + BLE adv/scan running.\n", ESPNOW_CHANNEL);
}

void loop() {
  now.loop();   // ping tx + (no-op once locked) scan/lock plumbing

  static uint32_t last = 0;
  if (millis() - last >= 2000) {
    last = millis();
    String msg = "hi @" + String(millis() / 1000);
    now.broadcast(msg);
    // now.sendAll(msg);   // unicast variant: sends to every addPeers()'d MAC
    ble.broadcast(msg);
  }
}
