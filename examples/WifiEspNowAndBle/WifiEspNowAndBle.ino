// WifiEspNowAndBle.ino - Wi-Fi STA + HTTP + ESP-NOW + BLE on one ESP32.
// Init order matters for coex: 1. Wi-Fi STA  2. ESP-NOW  3. BLE.

#include <EspWiFiBLENow.h>
#include <WebServer.h>
#include <WiFi.h>

const char* WIFI_SSID = "hamza";
const char* WIFI_PASS = "123";

EspNow     now;
BLE        ble;
WebServer  http(80);

void setupWifiAndHttp() {
  if (Wifi::beginSTA(WIFI_SSID, WIFI_PASS, 15000, /*longRange=*/false)) {
    Serial.printf("[SYS ] Wi-Fi ip=%s ch=%u\n",
                  Wifi::staIP().toString().c_str(), Wifi::channel());
  } else {
    Serial.println("[SYS ] Wi-Fi join failed");
  }

  EspWB::enableCors(http);
  http.on("/", []() { http.send(200, "text/plain", "esp-wifi-ble-now ok\n"); });
  http.on("/message", []() {
    String msg = http.arg("msg");
    Serial.printf("[HTTP %ddBm] %s -> %s\n",
                  (int)WiFi.RSSI(),
                  http.client().remoteIP().toString().c_str(),
                  msg.c_str());
    http.send(200, "text/plain", "ok");
  });
  http.begin();
  Serial.printf("[SYS ] HTTP up: http://%s/message?msg=hello\n",
                Wifi::staIP().toString().c_str());
}

void setupEspNow() {
  if (!now.begin(WIFI_IF_STA, /*longRange=*/false)) {
    Serial.println("[SYS ] ESP-NOW init failed");
    return;
  }
  now.setRepeats(10);            // 10x-tx (~60 ms airtime / call) - low loop block, dedup picks newest

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
  now.onLocked([](uint8_t ch) {
    Serial.printf("[NOW ] found peer, locked on ch %u\n", ch);
  });
  Serial.printf("[SYS ] ESP-NOW up (%s ch %u)\n",
                now.isScanning() ? "scanning" : "locked", now.channel());
}

void setupBle() {
  String bleMac = ble.begin(/*longRange=*/false);
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
  setupWifiAndHttp();
  setupEspNow();
  setupBle();
}

void loop() {
  http.handleClient();
  now.loop();                   // drives ESP-NOW channel scan until locked

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
    ble.broadcast((const uint8_t*)framed, ble.maxBroadcastLen());        //   24 B "<n> ...."
  }
}
