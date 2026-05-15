// EspNowAndBleNoWifi - ESP-NOW + BLE coexisting on the same ESP32, no Wi-Fi.
// Both peers must hard-code the SAME channel via now.begin(..., CH).
// Flash this same sketch on every node.

#include <EspWiFiBLENow.h>

constexpr uint8_t ESPNOW_CHANNEL = 6;     // pick any 1..13, identical on every node

EspNow now;
BLE    ble;

void setupEspNow() {
  // STA up but never associates. longRange=true => +21 dBm TX cap.
  // Passing a non-zero channel pins it and disables auto-scan.
  if (!now.begin(WIFI_IF_STA, /*longRange=*/true, ESPNOW_CHANNEL)) {
    Serial.println("[SYS ] ESP-NOW init failed");
    return;
  }
  now.addBroadcastPeer();
  now.setRepeats(3);            // BLE-style triple-tx (~30 ms airtime / call)

  // // ---- UNICAST MODE ----
  // // Replace broadcast with unicast to a fixed list of peers. Unicast uses
  // // ESP-NOW L2 ack/retry, no seq prefix, no dedup. setRepeats() doesn't apply.
  // static const uint8_t peers[][6] = {
  //   {0xC0,0x49,0xEF,0xD0,0x3F,0xE0},
  //   {0xC0,0x49,0xEF,0xD4,0x54,0x44},
  // };
  // now.addPeers(peers, sizeof(peers) / 6);

  now.onReceive([](const uint8_t mac[6], const uint8_t* data, size_t len, int8_t rssi) {
    Serial.printf("[NOW  %ddBm] %02x:%02x:%02x:%02x:%02x:%02x -> %.*s\n",
                  rssi, mac[0],mac[1],mac[2],mac[3],mac[4],mac[5], (int)len, data);
  });
  Serial.printf("[SYS ] ESP-NOW up (locked ch %u)\n", now.channel());
}

void setupBle() {
  // longRange=true => +9 dBm TX. Phone/browser still see it (no Coded PHY).
  String bleMac = ble.begin(/*longRange=*/true);
  if (bleMac.length()) Serial.printf("[BLE ] mac=%s\n", bleMac.c_str());

  ble.onReceive([](const uint8_t mac[6], const uint8_t* data, size_t len, int8_t rssi) {
    Serial.printf("[BLE  %ddBm] %02x:%02x:%02x:%02x:%02x:%02x -> %.*s\n",
                  rssi, mac[0],mac[1],mac[2],mac[3],mac[4],mac[5], (int)len, data);
  });
  ble.onMessage([](const uint8_t /*mac*/[6], const uint8_t* data, size_t len, int8_t) {
    Serial.printf("[WBLE     ] browser -> %.*s\n", (int)len, data);
  });
}

char msg_200byte[200]; // for testing max BLE (24B) and NOW (200B) payload

void setup() {
  Serial.begin(115200);
  delay(100);
  memset(msg_200byte, '.', sizeof(msg_200byte));
  setupEspNow();
  setupBle();
}

void loop() {
  now.loop();   // no-op once locked, kept for symmetry with the Wi-Fi example

  static uint32_t counter = 1, lastSend = 0;
  if (millis() - lastSend > 1000) {
    lastSend = millis();
    char framed[220];
    int n = snprintf(framed, sizeof(framed), "%lu ", (unsigned long)counter++);
    size_t room = sizeof(framed) - n;
    if (room > sizeof(msg_200byte)) room = sizeof(msg_200byte);
    memcpy(framed + n, msg_200byte, room);
    size_t total = n + room;
    now.broadcast((const uint8_t*)framed, total);                        // ~200 B "<n> ...."
    // now.sendAll((const uint8_t*)framed, total);                       // unicast to all addPeers()'d MACs
    vTaskDelay(10);
    ble.broadcast((const uint8_t*)framed, ble.maxBroadcastLen());        //   24 B "<n> ...."
  }
}
