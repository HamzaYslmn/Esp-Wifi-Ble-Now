#include "EspWB_EspNow.h"
#include "EspWB_Mac.h"
#include <esp_wifi.h>
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

inline bool    isPing(uint8_t b)    { uint8_t c = b & 0x0F; return (b & PING_FLAG) && c >= CH_MIN && c <= CH_MAX; }
inline uint8_t pingByte(uint8_t ch) { return PING_FLAG | (ch & 0x0F); }

struct RxSlot { uint8_t mac[6]; uint8_t data[RX_PAYLOAD]; size_t len; int8_t rssi; };

RxSlot           g_q[RX_QUEUE];
volatile size_t  g_qHead = 0, g_qTail = 0;
volatile int8_t  g_lastRssi = -127;
volatile bool    g_scanning = false;
volatile uint8_t g_pendingLockCh = 0;
wifi_interface_t g_iface  = WIFI_IF_STA;
uint8_t          g_channel = 0;
uint32_t         g_lastHopMs = 0, g_lastPingMs = 0;
EspNow*          g_instance = nullptr;
EspNow::OnRecv   g_userCb;
EspNow::OnLocked g_userLockedCb;

// MARK: broadcast multi-tx + dedup state
constexpr size_t   PEER_RING      = 8;
constexpr uint32_t REPEAT_GAP_MS  = 2;
struct PeerSeq { uint8_t mac[6]; uint8_t lastSeq; bool used; };
PeerSeq g_peers[PEER_RING] = {};
uint8_t g_txSeq   = 0;
uint8_t g_repeats = 1;

bool freshBcast(const uint8_t mac[6], uint8_t seq) {
  int slot = -1;
  for (size_t i = 0; i < PEER_RING; ++i) {
    if (g_peers[i].used && memcmp(g_peers[i].mac, mac, 6) == 0) {
      if (g_peers[i].lastSeq == seq) return false;
      g_peers[i].lastSeq = seq; return true;
    }
    if (!g_peers[i].used && slot < 0) slot = i;
  }
  if (slot < 0) slot = 0;                          // ring eviction
  memcpy(g_peers[slot].mac, mac, 6);
  g_peers[slot].lastSeq = seq;
  g_peers[slot].used    = true;
  return true;
}

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
    if (!ping) return;
    g_pendingLockCh = data[0] & 0x0F;
    g_scanning      = false;
    return;
  }
  if (ping) return;

  // Broadcast path: 1B seq prefix, dedup per source MAC.
  const bool wasBroadcast = info->des_addr && memcmp(info->des_addr, BCAST, 6) == 0;
  if (wasBroadcast) {
    if (len < 2) return;
    if (!freshBcast(info->src_addr, data[0])) return;
    data += 1; len -= 1;
  }

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
} // namespace

// MARK: lifecycle - LR PHY + LR-rate broadcast always on. longRange = +21 dBm TX boost.
// channel != 0 -> pinned (no auto-scan, no STA adoption). channel == 0 -> auto.
bool EspNow::begin(wifi_interface_t iface, bool longRange, uint8_t channel) {
  g_iface    = iface;
  g_instance = this;

  if (WiFi.getMode() == WIFI_OFF) WiFi.mode(WIFI_STA);

  esp_wifi_set_protocol(iface, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N | WIFI_PROTOCOL_LR);
  esp_wifi_set_ps(WIFI_PS_NONE);                  // mandatory: PS_MIN_MODEM parks RX between AP beacons -> drops broadcasts
  if (longRange) esp_wifi_set_max_tx_power(84);

  if (channel >= CH_MIN && channel <= CH_MAX) {  // user-pinned, no scan
    WiFi.setAutoReconnect(false);
    esp_wifi_disconnect();
    pinChannel(channel);
    g_scanning = false;
  } else if (WiFi.status() == WL_CONNECTED) {    // STA dictates channel
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

  if (esp_now_init() != ESP_OK) { esp_now_deinit(); if (esp_now_init() != ESP_OK) return false; }
  if (esp_now_register_recv_cb(onRecv) != ESP_OK) return false;
  addBroadcastPeer();

  // MARK: LR-rate broadcast (~3-4x sensitivity, 250 kbps cap)
  esp_now_rate_config_t cfg{ WIFI_PHY_MODE_LR, WIFI_PHY_RATE_LORA_250K, false, false };
  esp_err_t rateRc = esp_now_set_peer_rate_config(BCAST, &cfg);

  wifi_ps_type_t ps; esp_wifi_get_ps(&ps);
  uint8_t prim; wifi_second_chan_t sec; esp_wifi_get_channel(&prim, &sec);
  Serial.printf("[NOW ] begin: ps=%s ch=%u rate_cfg=%s longRange=%d\n",
                ps == WIFI_PS_NONE ? "NONE" : (ps == WIFI_PS_MIN_MODEM ? "MIN" : "MAX"),
                prim, rateRc == ESP_OK ? "OK" : "FAIL", (int)longRange);

  g_lastPingMs = millis();
  return true;
}

void EspNow::end() {
  g_scanning = false;
  esp_now_unregister_recv_cb();
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

bool EspNow::addBroadcastPeer()                 { return addPeer(BCAST, nullptr); }
bool EspNow::removePeer(const uint8_t mac[6])   { return esp_now_del_peer(mac) == ESP_OK; }
bool EspNow::setPmk(const uint8_t pmk16[16])    { return esp_now_set_pmk(pmk16) == ESP_OK; }

bool EspNow::addPeers(const uint8_t (*macs)[6], size_t count) {
  if (!macs || !count) return false;
  bool ok = true;
  for (size_t i = 0; i < count; ++i) ok = addPeer(macs[i]) && ok;
  return ok;
}

size_t EspNow::peerCount() const {
  esp_now_peer_num_t n{};
  if (esp_now_get_peer_num(&n) != ESP_OK) return 0;
  return esp_now_is_peer_exist(BCAST) ? (size_t)n.total_num - 1 : (size_t)n.total_num;
}

// MARK: send
bool EspNow::send(const uint8_t mac[6], const uint8_t* data, size_t len) {
  return data && len && len <= RX_PAYLOAD && esp_now_send(mac, data, len) == ESP_OK;
}
bool EspNow::send(const uint8_t mac[6], const String& s) { return send(mac, (const uint8_t*)s.c_str(), s.length()); }

bool EspNow::sendAll(const uint8_t* data, size_t len) {
  if (!data || !len || len > RX_PAYLOAD) return false;
  esp_now_peer_info_t p{};
  bool fromHead = true, any = false, ok = true;
  while (esp_now_fetch_peer(fromHead, &p) == ESP_OK) {
    fromHead = false;
    if (memcmp(p.peer_addr, BCAST, 6) == 0) continue;          // skip the broadcast pseudo-peer
    any = true;
    ok  = (esp_now_send(p.peer_addr, data, len) == ESP_OK) && ok;
  }
  return any && ok;
}
bool EspNow::sendAll(const String& s) { return sendAll((const uint8_t*)s.c_str(), s.length()); }

bool EspNow::broadcast(const uint8_t* data, size_t len) {
  if (!data || !len || len > RX_PAYLOAD - 1) return false;
  uint8_t buf[RX_PAYLOAD];
  buf[0] = ++g_txSeq;
  memcpy(buf + 1, data, len);
  bool ok = esp_now_send(BCAST, buf, len + 1) == ESP_OK;
  for (uint8_t i = 1; i < g_repeats; ++i) {
    delay(REPEAT_GAP_MS);
    esp_now_send(BCAST, buf, len + 1);
  }
  return ok;
}
bool EspNow::broadcast(const String& s) { return broadcast((const uint8_t*)s.c_str(), s.length()); }

void    EspNow::setRepeats(uint8_t n)  { g_repeats = n < 1 ? 1 : (n > 5 ? 5 : n); }
uint8_t EspNow::repeats()        const { return g_repeats; }

// MARK: polled rx
size_t EspNow::available() const { return (g_qHead - g_qTail + RX_QUEUE) % RX_QUEUE; }

bool EspNow::read(uint8_t mac[6], uint8_t* buf, size_t bufLen, size_t* outLen, int8_t* outRssi) {
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
void    EspNow::onReceive(OnRecv cb)   { g_userCb       = std::move(cb); }
void    EspNow::onLocked(OnLocked cb)  { g_userLockedCb = std::move(cb); }
int8_t  EspNow::lastRssi()       const { return g_lastRssi; }
uint8_t EspNow::channel()        const { return g_channel; }
bool    EspNow::isScanning()     const { return g_scanning; }

void    EspNow::getOwnMac(uint8_t out[6], wifi_interface_t iface) { esp_wifi_get_mac(iface, out); }
void    EspNow::printMac(Print& p, const uint8_t mac[6])          { espwbnPrintMac(p, mac); }
EspNow* EspNow::instance()                                        { return g_instance; }
