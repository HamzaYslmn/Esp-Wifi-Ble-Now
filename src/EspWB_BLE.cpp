#include "EspWB_BLE.h"
#include "EspWB_Mac.h"
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <esp_mac.h>

namespace {
// MARK: wire format - manufacturer data: FF FF 'E' 'W' seq[1] payload (in scan-response only).
constexpr uint8_t MAGIC0     = 'E';
constexpr uint8_t MAGIC1     = 'W';
constexpr size_t  MFR_HEADER = 5;        // FF FF E W seq

// MARK: NUS - Nordic UART Service (browser <-> ESP32 over Web Bluetooth)
constexpr char NUS_SERVICE[] = "6E400001-B5A3-F393-E0A9-E50E24DCCA9E";
constexpr char NUS_RX[]      = "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"; // browser -> ESP32
constexpr char NUS_TX[]      = "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"; // ESP32 -> browser

BLEAdvertising*    g_adv    = nullptr;
BLEScan*           g_scan   = nullptr;
BLEServer*         g_server = nullptr;
BLECharacteristic* g_txChar = nullptr;
BLECharacteristic* g_rxChar = nullptr;
volatile bool      g_connected = false;
uint8_t            g_peerMac[6] = {0};
BLE::OnRx          g_userRx;
BLE::OnMsg         g_userMsg;
bool               g_active = false;
uint8_t            g_seq = 0;
String             g_ownMacStr;          // this node's MAC, tags our own NUS notifies
String             g_devName;            // "EspWB-XXXX" (XXXX = last 2 hex of BT MAC)

// MARK: dedup - per-peer last seq collapses repeated adv packets (~10/sec).
struct Peer { uint8_t mac[6]; uint8_t seq; bool used; };
Peer g_peers[8];

bool fresh(const uint8_t mac[6], uint8_t seq) {
  int slot = -1;
  for (int i = 0; i < 8; ++i) {
    if (g_peers[i].used && memcmp(g_peers[i].mac, mac, 6) == 0) {
      if (g_peers[i].seq == seq) return false;
      g_peers[i].seq = seq;
      return true;
    }
    if (!g_peers[i].used && slot < 0) slot = i;
  }
  if (slot < 0) slot = 0;
  memcpy(g_peers[slot].mac, mac, 6);
  g_peers[slot].seq  = seq;
  g_peers[slot].used = true;
  return true;
}

// MARK: GATT callbacks
class ServerCb : public BLEServerCallbacks {
  void onConnect(BLEServer*, esp_ble_gatts_cb_param_t* param) override {
    g_connected = true;
    if (param) memcpy(g_peerMac, param->connect.remote_bda, 6);
    if (g_scan) g_scan->stop();                        // free BLE controller for GATT events
  }
  void onDisconnect(BLEServer*) override {
    g_connected = false;
    memset(g_peerMac, 0, 6);
    if (g_adv)  g_adv->start();                        // browsers can reconnect
    if (g_scan) g_scan->start(0, nullptr, false);      // resume peer-adv scan
  }
};

class RxCb : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* c) override {
    String v = c->getValue();
    if (g_userMsg && v.length())
      g_userMsg(g_peerMac, (const uint8_t*)v.c_str(), v.length(), 0);
  }
};

ServerCb g_serverCb;
RxCb     g_rxCb;

// MARK: scan - decode mfr ad, dedupe, fire callback, relay to connected browser.
class ScanCb : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice dev) override {
    if (!dev.haveManufacturerData()) return;
    String mfr = dev.getManufacturerData();
    if (mfr.length() <= MFR_HEADER) return;
    const uint8_t* m = (const uint8_t*)mfr.c_str();
    if (m[0] != 0xFF || m[1] != 0xFF || m[2] != MAGIC0 || m[3] != MAGIC1) return;

    uint8_t seq = m[4];
    String macStr = dev.getAddress().toString();
    uint8_t mac[6];
    espwbnParseMac(macStr.c_str(), mac);
    if (!fresh(mac, seq)) return;

    const uint8_t* payload = m + MFR_HEADER;
    size_t plen = mfr.length() - MFR_HEADER;
    if (g_userRx) g_userRx(mac, payload, plen, (int8_t)dev.getRSSI());

    if (g_txChar && g_connected) {
      String line = macStr + " ";
      line.concat((const char*)payload, plen);
      line += "\n";
      g_txChar->setValue((uint8_t*)line.c_str(), line.length());
      g_txChar->notify();
    }
  }
};
ScanCb g_scanCb;

void setupNusServer() {
  g_server = BLEDevice::createServer();
  g_server->setCallbacks(&g_serverCb);
  BLEService* svc = g_server->createService(NUS_SERVICE);

  g_txChar = svc->createCharacteristic(NUS_TX, BLECharacteristic::PROPERTY_NOTIFY);
  g_txChar->addDescriptor(new BLE2902());              // Bluedroid needs CCCD explicitly

  g_rxChar = svc->createCharacteristic(
      NUS_RX, BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
  g_rxChar->setCallbacks(&g_rxCb);
  svc->start();
}

void setAdv(const uint8_t* data, size_t len) {
  if (len > BLE::maxBroadcastLen()) len = BLE::maxBroadcastLen();

  String mfr;
  mfr.reserve(MFR_HEADER + len);
  mfr += (char)0xFF; mfr += (char)0xFF;
  mfr += (char)MAGIC0; mfr += (char)MAGIC1; mfr += (char)(++g_seq);
  for (size_t i = 0; i < len; ++i) mfr += (char)data[i];

  // Adv (~15B): flags + complete name "EspWB-XXXX". Browser chooser shows the name.
  BLEAdvertisementData adv;
  adv.setFlags(0x06);                                  // LE General Disc, BR/EDR off
  adv.setName(g_devName.c_str());                      // unique per node
  g_adv->setAdvertisementData(adv);

  // Scan-rsp (~29B): mfr payload (FF FF E W seq + up to 24 user bytes).
  BLEAdvertisementData rsp;
  rsp.setManufacturerData(mfr);
  g_adv->setScanResponseData(rsp);
}
} // namespace

// MARK: lifecycle
String BLE::begin(bool longRange) {
  if (g_active) return BLEDevice::getAddress().toString().c_str();

  // Build a unique name from the BT MAC before BLEDevice::init (which sets the GAP name).
  uint8_t btmac[6] = {0};
  esp_read_mac(btmac, ESP_MAC_BT);
  char nameBuf[16];
  snprintf(nameBuf, sizeof(nameBuf), "EspWB-%02X%02X", btmac[4], btmac[5]);
  g_devName = nameBuf;

  BLEDevice::init(g_devName.c_str());
  BLEDevice::setMTU(247);                              // 247 ATT -> 244 B per NUS write/notify (default 23->20)
  g_ownMacStr = BLEDevice::getAddress().toString().c_str();

  if (longRange) BLEDevice::setPower(ESP_PWR_LVL_P9);  // +9 dBm, all power types

  setupNusServer();

  g_adv = BLEDevice::getAdvertising();
  setAdv(nullptr, 0);
  g_adv->start();                                      // beacon for browsers

  g_scan = BLEDevice::getScan();
  g_scan->setInterval(160);                            // 100 ms period
  g_scan->setWindow(16);                               // 10 ms scan = 10% duty -> ~90% RF for ESP-NOW (BLE adv fires every 20-40 ms anyway)
  g_scan->setAdvertisedDeviceCallbacks(&g_scanCb, true /*wantDuplicates*/);
  g_scan->setActiveScan(true);                         // active - pulls peer's scan-response for full mfr payload
  g_scan->start(0, nullptr, false);                    // 0 = continuous (paused while GATT connected)

  g_active = true;
  return BLEDevice::getAddress().toString().c_str();
}

void BLE::end() {
  if (!g_active) return;
  if (g_scan) g_scan->stop();
  if (g_adv)  g_adv->stop();
  BLEDevice::deinit(true);
  g_adv = nullptr; g_scan = nullptr; g_server = nullptr;
  g_txChar = nullptr; g_rxChar = nullptr;
  g_connected = false; g_active = false;
}

// MARK: broadcast - update adv payload, or NUS-notify if browser is connected.
bool BLE::broadcast(const uint8_t* data, size_t len) {
  if (!g_active || len > maxBroadcastLen()) return false;

  if (g_connected && g_txChar) {
    String line = g_ownMacStr + " ";
    line.concat((const char*)data, len);
    line += "\n";
    g_txChar->setValue((uint8_t*)line.c_str(), line.length());
    g_txChar->notify();
    return true;
  }

  setAdv(data, len);
  return true;
}

bool BLE::broadcast(const String& s) {
  return broadcast((const uint8_t*)s.c_str(), s.length());
}

void BLE::onReceive(OnRx cb)  { g_userRx  = std::move(cb); }
void BLE::onMessage(OnMsg cb) { g_userMsg = std::move(cb); }

bool BLE::isConnected() const { return g_connected; }

// MARK: helpers
void BLE::getOwnMac(uint8_t out[6]) {
  espwbnParseMac(BLEDevice::getAddress().toString().c_str(), out);
}

void BLE::printMac(Print& p, const uint8_t mac[6]) { espwbnPrintMac(p, mac); }
