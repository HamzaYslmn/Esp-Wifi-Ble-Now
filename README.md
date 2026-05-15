# Esp-WiFi-BLE-Now

**Wi-Fi + ESP-NOW + BLE on one ESP32**, in one tiny sketch. "Now" = Espressif's connectionless ESP-NOW.

Web console: **https://hamzayslmn.github.io/Esp-Wifi-Ble-Now/**

Most of this is a thin layer over the stock Arduino-ESP32 APIs (`WiFi.h`, `esp_now.h`, `BLEDevice.h`). Anything you can do natively still works alongside this library.

---

## Install

Drop the folder into `Documents/Arduino/libraries/`. First flash → **Erase Flash → "All Flash Contents"** once (Bluedroid NVS bond namespace).

Board: any ESP32, arduino-esp32 v3.3.6. No external deps.

---

## Usage scenarios

### 1. All three radios at once (the main example)
[`examples/WifiEspNowAndBle/WifiEspNowAndBle.ino`](examples/WifiEspNowAndBle/WifiEspNowAndBle.ino) — Wi-Fi STA + HTTP + ESP-NOW + BLE, broadcasting a counter every second.

Init order matters for coex: **Wi-Fi → ESP-NOW → BLE**.

```cpp
Wifi::beginSTA("ssid", "pass");
now.begin();  now.addBroadcastPeer();
ble.begin();
```

### 2. ESP-NOW broadcast between ESPs
```cpp
EspNow now;
now.begin();                       // 1 Mbps PHY, default TX power (pass longRange=true for LR + max TX)
now.onReceive([](auto mac, auto d, auto n, auto rssi){ /* ... */ });
now.onLocked([](uint8_t ch){ Serial.printf("locked ch %u\n", ch); });
now.broadcast("hello");

void loop() { now.loop(); /* ... */ }   // drives auto channel discovery + pings
```

Channel handling is automatic:
- **Wi-Fi STA up:** ESP-NOW adopts the router's channel.
- **Wi-Fi down:** the node scans channels 1..13 and locks onto the first one it hears a peer on, staying there until reboot.

To make discovery work without the user sending anything, every node broadcasts a 1-byte ping at 1 Hz (`0x80 | channel`). Pings are filtered out of `onReceive`. Reserved 1-byte payload range: `0x81..0x8D`.

### 3. BLE broadcast (peer ESPs see it via this lib, browsers see the NUS server)
```cpp
BLE ble;
ble.begin();                       // 1M PHY, default power -- phone/browser visible
ble.onReceive(...);                // adv from another ESP running this lib
ble.onMessage(...);                // GATT write from a browser
ble.broadcast("hi");               // <= 18 bytes in one adv packet
```

### 4. Wi-Fi STA + HTTP with CORS that just works
```cpp
WebServer http(80);
Wifi::beginSTA("ssid", "pass");
EspWB::enableCors(http);           // CORS + Chrome PNA, call BEFORE http.on(...)
http.on("/", [](){ http.send(200, "text/plain", "ok"); });
http.begin();
```

### 5. Maximum range
Each `begin()` takes a `longRange` flag that bumps **TX power only**:

```cpp
Wifi::beginSTA("ssid", "pass", 15000, /*longRange=*/true);   // +20 dBm
ble.begin(/*longRange=*/true);                                // +9 dBm
now.begin(6, WIFI_IF_STA, /*longRange=*/true);                // LR PHY + max TX power
```

`longRange` does **not** change BLE's PHY — older ESP32 silicon doesn't support BLE 5 Coded PHY. If your chip *does* (ESP32-C3/S3/C6/H2) and you want even more range on BLE, add Coded PHY natively after `ble.begin()`:

```cpp
#include <esp_gap_ble_api.h>
ble.begin(true);                   // +9 dBm
esp_ble_gap_set_preferred_default_phy(ESP_BLE_GAP_PHY_CODED_PREF_MASK,
                                      ESP_BLE_GAP_PHY_CODED_PREF_MASK);
```
Note: with Coded PHY active, phones / Web Bluetooth can no longer see the advertiser.

### 6. Drop ESP-NOW back to plain B/G/N
```cpp
now.begin(1, WIFI_IF_STA, /*longRange=*/false);   // no LR PHY, default TX power
```
The `channel` arg is only used as a starting point when Wi-Fi STA is down (and even then, scanning will move off it). When Wi-Fi STA is up, the router's channel wins.

---

## What this library adds that isn't in the native APIs

These are the only places you can't just read `esp_now.h` / `BLEDevice.h` and know what's going on:

1. **`BLE::broadcast` wire format** — payload sits in manufacturer-specific adv data as `FF FF 'E' 'W' seq[1] payload[<=18]`. Receivers in this library decode that shape; receivers in other apps won't. Sequence-based dedupe collapses the ~10 adv-emits/sec into one `onReceive(...)`.
2. **NUS GATT relay** — while a GATT client is connected (e.g. browser), every received broadcast is also notified out on the NUS TX char as `"<mac> <payload>\n"`. Lets a phone/dashboard see what your ESP's BLE neighbours are saying.
   - Service `6E400001-…`, RX (write) `…0002-…`, TX (notify) `…0003-…`
3. **ESP-NOW `longRange` default = `false`** — LR PHY pegs frames to ~250 kbps, ~10× longer airtime than 1 Mbps, which collides badly with BLE scan on a Wi-Fi-less peer (ESP-IDF coex marks `ESP-NOW RX + BLE Scan` as *"stable in STA mode, otherwise not supported"*). Default is short frames; pass `true` if you need range and accept the airtime cost.
4. **ESP-NOW auto channel discovery** — Wi-Fi-less nodes hop channels 1..13 (500 ms dwell) and lock onto the channel embedded in the first peer's 1-byte ping (`0x80 | channel`). Removes the "both devices must be on the same channel" foot-gun. `onLocked(ch)` fires once when the lock latches.
5. **`EspWB::enableCors(WebServer&)`** — one call installs `Access-Control-Allow-Origin: *` plus Chrome PNA preflight handling. Just sugar around `WebServer::addMiddleware`.
6. **`longRange=true` on `BLE::begin` / `Wifi::beginSTA`** — does exactly `BLEDevice::setPower(ESP_PWR_LVL_P9)` / `esp_wifi_set_max_tx_power(84)`. Convenience, nothing magic.

Everything else (peer mgmt, send, RSSI, MAC helpers, scan, GATT server lifecycle) is the stock arduino-esp32 surface.

---

## Limits

- **18-byte BLE broadcast cap** — one legacy adv packet's worth of manufacturer data. Larger → use NUS GATT, HTTP, or ESP-NOW (250 B).
- **No re-airing** — if nobody scanned while the adv was on the air, it's gone. Add app-level acks if you need delivery.
- **Shared TX power** — Wi-Fi and ESP-NOW share the radio's TX power. ESP-NOW's default already maxes it; Wi-Fi inherits.
- **BLE device name** is `"EspWB"` — change in [`src/EspWB_BLE.cpp`](src/EspWB_BLE.cpp) if you need distinct boards.

---

MIT. See [LICENSE](LICENSE).
