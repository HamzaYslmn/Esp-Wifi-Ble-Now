#include "EspWB_EspNow.h"
#include "EspWB_Mac.h"
#include <esp_wifi.h>
#include <esp_idf_version.h>
#include <string.h>

namespace {
// MARK: rx queue
constexpr uint8_t BCAST[6]   = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
constexpr size_t  RX_QUEUE   = 8;
constexpr size_t  RX_PAYLOAD = 250;

struct RxSlot {
  uint8_t mac[6];
  uint8_t data[RX_PAYLOAD];
  size_t  len;
  int8_t  rssi;
};

RxSlot           g_q[RX_QUEUE];
volatile size_t  g_qHead = 0;
volatile size_t  g_qTail = 0;
volatile int8_t  g_lastRssi = -127;
wifi_interface_t g_iface = WIFI_IF_STA;
EspNow*          g_instance = nullptr;
EspNow::OnRecv   g_userCb;

void IRAM_ATTR onRecv(const esp_now_recv_info_t* info,
                      const uint8_t* data, int len) {
  if (!info || !data || len <= 0) return;
  int8_t rssi = (info->rx_ctrl ? info->rx_ctrl->rssi : (int8_t)-127);
  g_lastRssi = rssi;
  if (g_userCb) {
    g_userCb(info->src_addr, data, (size_t)len, rssi);
    return;
  }
  size_t next = (g_qHead + 1) % RX_QUEUE;
  if (next == g_qTail) {
    g_qTail = (g_qTail + 1) % RX_QUEUE; // drop oldest
  }
  RxSlot& s = g_q[g_qHead];
  memcpy(s.mac, info->src_addr, 6);
  size_t cp = (size_t)len < RX_PAYLOAD ? (size_t)len : RX_PAYLOAD;
  memcpy(s.data, data, cp);
  s.len  = cp;
  s.rssi = rssi;
  g_qHead = next;
}

// MARK: send cb - signature changed in IDF 5.4 (uint8_t* mac -> wifi_tx_info_t*)
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 4, 0)
void onSent(const wifi_tx_info_t* /*info*/, esp_now_send_status_t /*status*/) {}
#else
void onSent(const uint8_t* /*mac*/, esp_now_send_status_t /*status*/) {}
#endif
} // namespace

// MARK: lifecycle
bool EspNow::begin(uint8_t channel, wifi_interface_t iface, bool longRange) {
  g_iface    = iface;
  g_instance = this;

  if (WiFi.getMode() == WIFI_OFF) WiFi.mode(WIFI_STA);

  uint8_t proto = WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N;
  if (longRange) proto |= WIFI_PROTOCOL_LR;
  esp_wifi_set_protocol(iface, proto);

  // If Wi-Fi STA is already connected to a router, keep its channel - changing
  // it would drop the AP. Otherwise lock to the requested channel.
  if (WiFi.status() != WL_CONNECTED) {
    esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
  }

  if (esp_now_init() != ESP_OK) {
    esp_now_deinit();
    if (esp_now_init() != ESP_OK) return false;
  }
  if (esp_now_register_recv_cb(onRecv) != ESP_OK) return false;
  if (esp_now_register_send_cb(onSent) != ESP_OK) return false;
  return true;
}

void EspNow::end() {
  esp_now_unregister_recv_cb();
  esp_now_unregister_send_cb();
  esp_now_deinit();
  g_instance = nullptr;
  g_userCb   = nullptr;
}

// MARK: peers
bool EspNow::addPeer(const uint8_t mac[6], const uint8_t* lmk16) {
  esp_now_peer_info_t p{};
  memcpy(p.peer_addr, mac, 6);
  p.ifidx   = g_iface;
  p.channel = 0;
  p.encrypt = (lmk16 != nullptr);
  if (lmk16) memcpy(p.lmk, lmk16, 16);
  if (esp_now_is_peer_exist(mac)) return esp_now_mod_peer(&p) == ESP_OK;
  return esp_now_add_peer(&p) == ESP_OK;
}

bool EspNow::addBroadcastPeer() { return addPeer(BCAST, nullptr); }

bool EspNow::removePeer(const uint8_t mac[6]) {
  return esp_now_del_peer(mac) == ESP_OK;
}

bool EspNow::setPmk(const uint8_t pmk16[16]) {
  return esp_now_set_pmk(pmk16) == ESP_OK;
}

// MARK: send
bool EspNow::send(const uint8_t mac[6], const uint8_t* data, size_t len) {
  if (!data || !len || len > RX_PAYLOAD) return false;
  return esp_now_send(mac, data, len) == ESP_OK;
}

bool EspNow::send(const uint8_t mac[6], const String& s) {
  return send(mac, (const uint8_t*)s.c_str(), s.length());
}

bool EspNow::broadcast(const uint8_t* data, size_t len) {
  return send(BCAST, data, len);
}

bool EspNow::broadcast(const String& s) {
  return send(BCAST, s);
}

// MARK: polled rx
size_t EspNow::available() const {
  return (g_qHead - g_qTail + RX_QUEUE) % RX_QUEUE;
}

bool EspNow::read(uint8_t mac[6], uint8_t* buf, size_t bufLen,
                  size_t* outLen, int8_t* outRssi) {
  if (g_qHead == g_qTail) return false;
  RxSlot& s = g_q[g_qTail];
  if (mac) memcpy(mac, s.mac, 6);
  size_t cp = s.len < bufLen ? s.len : bufLen;
  if (buf && bufLen) memcpy(buf, s.data, cp);
  if (outLen)  *outLen  = cp;
  if (outRssi) *outRssi = s.rssi;
  g_qTail = (g_qTail + 1) % RX_QUEUE;
  return true;
}

// MARK: helpers
void EspNow::onReceive(OnRecv cb) { g_userCb = std::move(cb); }

int8_t EspNow::lastRssi() const { return g_lastRssi; }

void EspNow::getOwnMac(uint8_t out[6], wifi_interface_t iface) {
  esp_wifi_get_mac(iface, out);
}

void EspNow::printMac(Print& p, const uint8_t mac[6]) { espwbnPrintMac(p, mac); }

EspNow* EspNow::instance() { return g_instance; }
