# Esp-WiFi-BLE-Now

Run **Wi-Fi + ESP-NOW + BLE all together** on a single ESP32, in one tiny Arduino sketch.

The "Now" in the name is Espressif's connectionless **ESP-NOW** protocol.

Web console: **https://hamzayslmn.github.io/Esp-Wifi-Ble-Now/**

---

## What you get

- **Wi-Fi** (`Wifi::`) — STA, AP, or AP+STA via a thin namespace wrapper around `WiFi.h`.
- **ESP-NOW** (`EspNow`) — multi-peer send/broadcast, callback or polled RX, RSSI per packet, optional encryption, optional long-range PHY.
- **BLE** (`BLE`, Bluedroid) — three things in one class:
  1. **Always-on broadcast** (`ble.broadcast(data)`): pushes a short payload into a manufacturer-specific advertising packet. Peers running this library decode it via `onReceive(...)`.
  2. **Passive scanner** that decodes broadcasts from other ESP32s running this library.
  3. **NUS GATT server** (Nordic UART Service) so a phone or browser (Web Bluetooth) can connect and write messages, which arrive via `onMessage(...)`.
- **HTTP helper** (`EspWB::enableCors`) — one call that adds CORS + Chrome Private-Network-Access headers to a stock `WebServer` so a browser at `localhost`/HTTPS can hit your ESP without preflight pain.

All three radios run **simultaneously**. We trust the ESP-IDF software coexistence arbiter — no manual scan/adv interval tweaking.

---

## How coexistence works

Wi-Fi (2.4 GHz) and BLE share the same radio. arduino-esp32 ships with the ESP-IDF SW coexistence arbiter (`CONFIG_ESP_COEX_SW_COEXIST_ENABLE=y`) using the `BALANCE` preference by default — that's the right setting for general Wi-Fi+BLE. We don't override it.

ESP-NOW rides on the Wi-Fi MAC, so it always uses whichever channel Wi-Fi is currently on (the `channel` arg to `EspNow::begin()` is ignored when STA is already connected).

**Init order matters** so coex sees the radios in the right order:

1. `Wifi::beginSTA(...)` — locks the Wi-Fi channel
2. `now.begin()` — inherits the STA channel
3. `ble.begin()` — BLE controller + Bluedroid host

BLE TX power is set to **+9 dBm** (`ESP_PWR_LVL_P9`, the chip max) on every power type for the longest possible range.

---

## Install

1. Drop this folder into `Documents/Arduino/libraries/`.
2. **First flash:** Tools → **Erase Flash → "All Flash Contents"** once. Bluedroid stores bonds in NVS; a stale namespace from another BLE library can trip a boot assert.

Board: any ESP32 (classic dual-core). arduino-esp32 v3.3.6.

No external dependencies — Bluedroid, Wi-Fi, ESP-NOW are all built in.

---

## The example

There is one example: [`examples/WifiEspNowAndBle/WifiEspNowAndBle.ino`](examples/WifiEspNowAndBle/WifiEspNowAndBle.ino). It brings up all three radios + an HTTP `/message` route, and broadcasts a counter every second on both ESP-NOW and BLE.

Skeleton:

```cpp
#include <EspWiFiBLENow.h>
#include <WebServer.h>
#include <WiFi.h>

EspNow    now;
BLE       ble;
WebServer http(80);

void setup() {
  Serial.begin(115200);

  // 1. Wi-Fi STA  (locks the channel)
  Wifi::beginSTA("ssid", "pass");

  // 2. ESP-NOW    (inherits STA channel)
  now.begin();
  now.addBroadcastPeer();
  now.onReceive([](const uint8_t mac[6], const uint8_t* d, size_t n, int8_t rssi) {
    Serial.printf("[NOW %ddBm] %.*s\n", rssi, (int)n, d);
  });

  // 3. BLE        (returns own MAC string, "" on fail)
  String bleMac = ble.begin();
  ble.onReceive([](const uint8_t mac[6], const uint8_t* d, size_t n, int8_t rssi) {
    Serial.printf("[BLE %ddBm] %.*s\n", rssi, (int)n, d);
  });
  ble.onMessage([](const uint8_t mac[6], const uint8_t* d, size_t n, int8_t) {
    Serial.printf("[WBLE] browser -> %.*s\n", (int)n, d);
  });

  // 4. HTTP with CORS + Chrome PNA, then routes
  EspWB::enableCors(http);
  http.on("/message", []() {
    String msg = http.arg("msg");
    Serial.printf("[HTTP %ddBm] %s -> %s\n",
                  (int)WiFi.RSSI(),
                  http.client().remoteIP().toString().c_str(),
                  msg.c_str());
    http.send(200, "text/plain", "ok");
  });
  http.begin();
}

void loop() {
  http.handleClient();
  static uint32_t n = 0, t = 0;
  if (millis() - t > 1000) {
    t = millis();
    String s = String(++n);
    now.broadcast(s);
    ble.broadcast(s);
  }
}
```

---

## API

### `Wifi::` (namespace)
```cpp
bool      beginSTA(ssid, pass, timeoutMs = 15000);
bool      beginAP(ssid, pass = nullptr, channel = 1, hidden = false);
bool      beginAPSTA(apSsid, apPass, staSsid, staPass, apChannel = 1, staTimeoutMs = 15000);
uint8_t   channel();          // current primary channel
IPAddress staIP();
IPAddress apIP();
```

### `EspNow` (class)
```cpp
bool   begin(channel = 6, iface = WIFI_IF_STA, longRange = false);
void   end();

bool   addPeer(mac, lmk16 = nullptr);   // lmk16=null => no encryption
bool   addBroadcastPeer();
bool   removePeer(mac);
static bool setPmk(pmk16);

bool   send(mac, data, len);            // up to ESP_NOW_MAX_DATA_LEN (250)
bool   broadcast(data, len);            // FF:FF:FF:FF:FF:FF

void   onReceive(cb);                   // (mac, data, len, rssi)
size_t available() const;               // polled fallback when no cb
bool   read(mac, buf, bufLen, *outLen, *outRssi);

int8_t lastRssi() const;
static void getOwnMac(out[6], iface = WIFI_IF_STA);
static void printMac(Print&, mac);
static EspNow* instance();
```

### `BLE` (class)
```cpp
String begin();                          // returns own MAC ("AA:BB:..."), "" on fail
void   end();

bool broadcast(const uint8_t* data, size_t len);   // <= 18 bytes
bool broadcast(const String& s);

void onReceive(OnRx);    // (mac, data, len, rssi)  - from another ESP running this lib
void onMessage(OnMsg);   // (mac, data, len, rssi)  - from a connected GATT client (browser/phone)

bool isConnected() const;                // GATT client connected?
static constexpr size_t maxBroadcastLen() { return 18; }

static void getOwnMac(uint8_t out[6]);
static void printMac(Print&, const uint8_t mac[6]);
```

### `EspWB::` (namespace)
```cpp
void enableCors(WebServer& s);   // adds CORS + PNA middleware. Call BEFORE http.on(...).
```

### BLE wire format (broadcast)
Manufacturer-specific data inside the advertising packet:

```
FF FF 'E' 'W' seq[1] payload[0..18]
```

`seq` increments on every `setAdv` call. Receivers dedupe by `(mac, seq)` so the multiple packets a single advertisement emits collapse into one `onReceive(...)` call.

### NUS UUIDs
- Service: `6E400001-B5A3-F393-E0A9-E50E24DCCA9E`
- RX (write):   `6E400002-B5A3-F393-E0A9-E50E24DCCA9E`
- TX (notify):  `6E400003-B5A3-F393-E0A9-E50E24DCCA9E`

When something is connected over GATT, every received broadcast is also relayed out on the TX characteristic as `"<mac> <seq> <payload>\n"` — handy for a browser dashboard.

A simple Web Bluetooth dashboard lives in [`web/`](web/) — live at
**https://hamzayslmn.github.io/Esp-Wifi-Ble-Now/**

---

## Limits / tradeoffs

- **18-byte BLE broadcast cap** — that's what fits in the manufacturer data of one legacy adv packet. Larger payloads → use NUS GATT or HTTP or ESP-NOW (250 B).
- **No re-airing** — if nobody was scanning when an adv was on the air, the message is lost. Build acks at the application level if you need delivery guarantees.
- **+9 dBm draws more current** — set in [`src/EspWB_BLE.cpp`](src/EspWB_BLE.cpp) (`ESP_PWR_LVL_P9`). Drop to `ESP_PWR_LVL_N0` (0 dBm) if you're battery-powered and don't need the range.
- **One BLE device name** (`"EspWB"`) — change in [`src/EspWB_BLE.cpp`](src/EspWB_BLE.cpp) if you need multiple boards distinguishable in scanners.

---

## License

MIT. See [LICENSE](LICENSE).
