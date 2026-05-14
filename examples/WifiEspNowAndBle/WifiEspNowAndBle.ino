// WifiEspNowAndBle.ino - ALL THREE radios on one ESP32:
//   - Wi-Fi STA + HTTP server
//   - ESP-NOW broadcast (uses Wi-Fi STA's channel automatically)
//   - BLE broadcast + GATT NUS server
//
// Init order (matters for the ESP-IDF coex arbiter):
//   1. Wi-Fi STA   (locks the channel)
//   2. ESP-NOW     (inherits the STA channel)
//   3. BLE         (controller + Bluedroid host)

#include <EspWiFiBLENow.h>
#include <WebServer.h>
#include <WiFi.h>

const char* WIFI_SSID = "hamza";
const char* WIFI_PASS = "123";

EspNow      now;
BLE         ble;
WebServer   http(80);

void setup() {
  Serial.begin(115200);
  delay(100);

  // 1. Wi-Fi STA
  if (Wifi::beginSTA(WIFI_SSID, WIFI_PASS)) {
    Serial.printf("[SYS ] Wi-Fi ip=%s ch=%u\n",
                  Wifi::staIP().toString().c_str(), Wifi::channel());
  } else {
    Serial.println("[SYS ] Wi-Fi join failed");
  }

  // 2. ESP-NOW (inherits STA channel since Wi-Fi is up)
  if (now.begin()) {
    now.addBroadcastPeer();
    now.onReceive([](const uint8_t mac[6], const uint8_t* data, size_t len, int8_t rssi) {
      Serial.printf("[NOW  %ddBm] %02x:%02x:%02x:%02x:%02x:%02x -> %.*s\n",
                    rssi, mac[0],mac[1],mac[2],mac[3],mac[4],mac[5], (int)len, data);
    });
    Serial.println("[SYS ] ESP-NOW up");
  }

  // 3. BLE
  String bleMac = ble.begin();
  if (bleMac.length()) Serial.printf("[BLE ] mac=%s\n", bleMac.c_str());
  ble.onReceive([](const uint8_t mac[6], const uint8_t* data, size_t len, int8_t rssi) {
    Serial.printf("[BLE  %ddBm] %02x:%02x:%02x:%02x:%02x:%02x -> %.*s\n",
                  rssi, mac[0],mac[1],mac[2],mac[3],mac[4],mac[5], (int)len, data);
  });
  ble.onMessage([](const uint8_t mac[6], const uint8_t* data, size_t len, int8_t /*rssi*/) {
    Serial.printf("[WBLE     ] browser -> %.*s\n", (int)len, data);
  });

  // 4. HTTP
  EspWB::enableCors(http);                  // CORS + Chrome PNA, before routes
  http.on("/", []() {
    http.send(200, "text/plain", "esp-wifi-ble-now ok\n");
  });
  http.on("/message", []() {
    String msg = http.arg("msg");
    IPAddress from = http.client().remoteIP();
    Serial.printf("[HTTP %ddBm] %s -> %s\n",
                  (int)WiFi.RSSI(), from.toString().c_str(), msg.c_str());
    http.send(200, "text/plain", "ok");
  });
  http.begin();
  Serial.printf("[SYS ] HTTP up: http://%s/message?msg=hello\n",
                Wifi::staIP().toString().c_str());
}

void loop() {
  http.handleClient();

  static uint32_t counter = 1, lastSend = 0;
  if (millis() - lastSend > 1000) {
    lastSend = millis();
    String msg = String(counter++);
    now.broadcast(msg);
    ble.broadcast(msg);
  }
}
