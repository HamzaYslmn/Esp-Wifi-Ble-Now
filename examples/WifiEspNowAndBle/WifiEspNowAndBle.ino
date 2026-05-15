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
  if (!now.begin(/*channel=*/6, WIFI_IF_STA, /*longRange=*/false)) {
    Serial.println("[SYS ] ESP-NOW init failed");
    return;
  }
  now.addBroadcastPeer();
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

void setup() {
  Serial.begin(115200);
  delay(100);
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
    String msg = String(counter++);
    now.broadcast(msg);
    ble.broadcast(msg);
  }
}
