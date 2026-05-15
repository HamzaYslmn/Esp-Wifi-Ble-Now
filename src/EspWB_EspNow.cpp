#include "EspWB_EspNow.h"
#include "EspWB_Mac.h"
#include <esp_wifi.h>
#include <esp_idf_version.h>
#include <string.h>

namespace {
constexpr uint8_t  BCAST[6]         = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
constexpr size_t   RX_QUEUE         = 8;
constexpr size_t   RX_PAYLOAD       = 250;
constexpr uint8_t  CH_MIN           = 1;
constexpr uint8_t  CH_MAX           = 13;
constexpr uint32_t SCAN_DWELL_MS    = 500;
constexpr uint32_t PING_INTERVAL_MS = 1000;
constexpr uint8_t  PING_FLAG        = 0x80;   // ping byte = PING_FLAG | channel(1..13)

inline bool    isPing(uint8_t b)      { uint8_t c = b & 0x0F; return (b & PING_FLAG) && c >= CH_MIN && c <= CH_MAX; }
inline uint8_t pingByte(uint8_t ch)   { return PING_FLAG | (ch & 0x0F); }

struct RxSlot { uint8_t mac[6]; uint8_t data[RX_PAYLOAD]; size_t len; int8_t rssi; };

RxSlot           g_q[RX_QUEUE];
volatile size_t  g_qHead = 0, g_qTail = 0;
volatile int8_t  g_lastRssi = -127;
volatile bool    g_scanning = false;
volatile uint8_t g_pendingLockCh = 0;        // nonzero => loop() should pin + notify
wifi_interface_t g_iface = WIFI_IF_STA;
uint8_t          g_channel = 0;
uint32_t         g_lastHopMs = 0, g_lastPingMs = 0;
EspNow*          g_instance = nullptr;
EspNow::OnRecv   g_userCb;
EspNow::OnLocked g_userLockedCb;

void pinChannel(uint8_t ch) {
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous(false);
  g_channel = ch;
}

bool due(uint32_t& last, uint32_t period) {
  uint32_t now = millis();
  if (now - last < period) return false;
  last = now;
  return true;
}

void IRAM_ATTR onRecv(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
  if (!info || !data || len <= 0) return;
  int8_t rssi = info->rx_ctrl ? info->rx_ctrl->rssi : (int8_t)-127;
  g_lastRssi = rssi;

  bool ping = (len == 1 && isPing(data[0]));
  if (g_scanning) {
    if (!ping) return;                          // only pings lock the channel
    g_pendingLockCh = data[0] & 0x0F;
    g_scanning      = false;
    return;
  }
  if (ping) return;                             // swallow post-lock pings
  if (g_userCb) { g_userCb(info->src_addr, data, (size_t)len, rssi); return; }

  size_t next = (g_qHead + 1) % RX_QUEUE;
  if (next == g_qTail) g_qTail = (g_qTail + 1) % RX_QUEUE;
  RxSlot& s = g_q[g_qHead];
  memcpy(s.mac, info->src_addr, 6);
  s.len = (size_t)len < RX_PAYLOAD ? (size_t)len : RX_PAYLOAD;
  memcpy(s.data, data, s.len);
  s.rssi  = rssi;
  g_qHead = next;
}

// IDF 5.4 changed signature (uint8_t* mac -> wifi_tx_info_t*)
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 4, 0)
void onSent(const wifi_tx_info_t*, esp_now_send_status_t) {}
#else
void onSent(const uint8_t*, esp_now_send_status_t) {}
#endif
} // namespace

// MARK: lifecycle
bool EspNow::begin(uint8_t channel, wifi_interface_t iface, bool longRange) {
  g_iface    = iface;
  g_instance = this;
  (void)channel;                                // unused: channel comes from STA or scan

  if (WiFi.getMode() == WIFI_OFF) WiFi.mode(WIFI_STA);

  uint8_t proto = WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N;
  if (longRange) { proto |= WIFI_PROTOCOL_LR; esp_wifi_set_max_tx_power(84); }
  esp_wifi_set_protocol(iface, proto);

  if (WiFi.status() == WL_CONNECTED) {          // STA dictates channel
    uint8_t prim; wifi_second_chan_t sec;
    esp_wifi_get_channel(&prim, &sec);
    g_channel  = prim;
    g_scanning = false;
  } else {                                       // hop until a peer's ping locks us
    WiFi.setAutoReconnect(false);
    esp_wifi_disconnect();
    pinChannel(CH_MIN);
    g_lastHopMs = millis();
    g_scanning  = true;
  }

  if (esp_now_init() != ESP_OK) {
    esp_now_deinit();
    if (esp_now_init() != ESP_OK) return false;
  }
  if (esp_now_register_recv_cb(onRecv) != ESP_OK) return false;
  if (esp_now_register_send_cb(onSent) != ESP_OK) return false;
  addBroadcastPeer();
  g_lastPingMs = millis();
  return true;
}

void EspNow::end() {
  g_scanning = false;
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
  p.encrypt = (lmk16 != nullptr);
  if (lmk16) memcpy(p.lmk, lmk16, 16);
  return esp_now_is_peer_exist(mac) ? esp_now_mod_peer(&p) == ESP_OK
                                    : esp_now_add_peer(&p) == ESP_OK;
}

bool EspNow::addBroadcastPeer() { return addPeer(BCAST, nullptr); }
bool EspNow::removePeer(const uint8_t mac[6]) { return esp_now_del_peer(mac) == ESP_OK; }
bool EspNow::setPmk(const uint8_t pmk16[16])  { return esp_now_set_pmk(pmk16) == ESP_OK; }

// MARK: send
bool EspNow::send(const uint8_t mac[6], const uint8_t* data, size_t len) {
  if (!data || !len || len > RX_PAYLOAD) return false;
  return esp_now_send(mac, data, len) == ESP_OK;
}
bool EspNow::send(const uint8_t mac[6], const String& s) { return send(mac, (const uint8_t*)s.c_str(), s.length()); }
bool EspNow::broadcast(const uint8_t* data, size_t len)  { return send(BCAST, data, len); }
bool EspNow::broadcast(const String& s)                  { return send(BCAST, s); }

// MARK: polled rx
size_t EspNow::available() const { return (g_qHead - g_qTail + RX_QUEUE) % RX_QUEUE; }

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

// MARK: loop - drives scan hop, ping tx, lock-pin + notify
void EspNow::loop() {
  if (g_scanning && due(g_lastHopMs, SCAN_DWELL_MS))
    pinChannel(g_channel >= CH_MAX ? CH_MIN : g_channel + 1);

  if (due(g_lastPingMs, PING_INTERVAL_MS)) {
    uint8_t b = pingByte(g_channel);
    esp_now_send(BCAST, &b, 1);
  }

  if (g_pendingLockCh) {
    uint8_t ch = g_pendingLockCh;
    g_pendingLockCh = 0;
    pinChannel(ch);
    if (g_userLockedCb) g_userLockedCb(ch);
  }
}

// MARK: helpers
void EspNow::onReceive(OnRecv cb)  { g_userCb       = std::move(cb); }
void EspNow::onLocked(OnLocked cb) { g_userLockedCb = std::move(cb); }

int8_t  EspNow::lastRssi()   const { return g_lastRssi; }
uint8_t EspNow::channel()    const { return g_channel; }
bool    EspNow::isScanning() const { return g_scanning; }

void EspNow::getOwnMac(uint8_t out[6], wifi_interface_t iface) { esp_wifi_get_mac(iface, out); }
void EspNow::printMac(Print& p, const uint8_t mac[6])          { espwbnPrintMac(p, mac); }
EspNow* EspNow::instance()                                     { return g_instance; }
