#pragma once
// Umbrella include - Wi-Fi + ESP-NOW + BLE on one ESP32.
// arduino-esp32 v3.3.6, BLE backend = bundled Bluedroid (BLEDevice.h).
//
// All three radios can run simultaneously. ESP-NOW shares the Wi-Fi MAC + STA
// channel; BLE rides on the same chip via the SW coex arbiter.

#define ESP_WIFI_BLE_NOW_VERSION "1.1.0"

#include "EspWB_WiFi.h"
#include "EspWB_BLE.h"
#include "EspWB_EspNow.h"
#include "EspWB_Http.h"
