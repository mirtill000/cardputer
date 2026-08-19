#include "SnmpSweep.h"
#include "ScanManager.h"
#include "../core/Types.h"
#include <WiFiUdp.h>
#include <vector>

SnmpSweep g_snmpSweep;

namespace {

// Wraps `content` in a BER TLV. Every length in an sysDescr.0 GET is
// well under 128, so single-byte definite length is always valid here.
std::vector<uint8_t> tlv(uint8_t tag, const std::vector<uint8_t>& content) {
    std::vector<uint8_t> out;
    out.push_back(tag);
    out.push_back((uint8_t)content.size());
    out.insert(out.end(), content.begin(), content.end());
    return out;
}

void appendAll(std::vector<uint8_t>& dst, const std::vector<uint8_t>& src) {
    dst.insert(dst.end(), src.begin(), src.end());
}

// Builds an SNMPv2c GET for sysDescr.0 with community "public".
std::vector<uint8_t> buildSysDescrGet() {
    // OID 1.3.6.1.2.1.1.1.0: first two subids (1,3) fold to 0x2b.
    std::vector<uint8_t> oid = {0x2b, 0x06, 0x01, 0x02, 0x01, 0x01, 0x01, 0x00};
    std::vector<uint8_t> varbind;
    appendAll(varbind, tlv(0x06, oid));            // OID
    appendAll(varbind, {0x05, 0x00});               // value = NULL
    std::vector<uint8_t> varbindSeq = tlv(0x30, varbind);
    std::vector<uint8_t> varbindList = tlv(0x30, varbindSeq);

    std::vector<uint8_t> pdu;
    appendAll(pdu, tlv(0x02, {0x00, 0x00, 0x00, 0x01}));  // request-id
    appendAll(pdu, tlv(0x02, {0x00}));                     // error-status
    appendAll(pdu, tlv(0x02, {0x00}));                     // error-index
    appendAll(pdu, varbindList);
    std::vector<uint8_t> getReq = tlv(0xA0, pdu);          // GetRequest PDU

    std::vector<uint8_t> msg;
    appendAll(msg, tlv(0x02, {0x01}));  // version = 1 (SNMPv2c)
    const char* community = "public";
    std::vector<uint8_t> comm(community, community + 6);
    appendAll(msg, tlv(0x04, comm));    // community
    appendAll(msg, getReq);
    return tlv(0x30, msg);              // outer SEQUENCE
}

// Reads a BER tag+length at `i`. On success sets tag, contentPos,
// contentLen and nextPos (just past the value) and returns true.
bool readTL(const uint8_t* buf, int len, int i, uint8_t& tag, int& contentPos, int& contentLen, int& nextPos) {
    if (i + 2 > len) return false;
    tag = buf[i];
    int j = i + 1;
    int l = buf[j++];
    if (l & 0x80) {
        int nbytes = l & 0x7F;
        if (nbytes == 0 || nbytes > 2 || j + nbytes > len) return false;  // no indefinite/huge lengths
        l = 0;
        for (int k = 0; k < nbytes; k++) l = (l << 8) | buf[j++];
    }
    if (j + l > len) return false;
    contentPos = j;
    contentLen = l;
    nextPos = j + l;
    return true;
}

// Navigates a GET-RESPONSE far enough to pull the first varbind's value.
// Returns true if the buffer is a well-formed SNMP response; sets
// sysDescr to the OCTET STRING value when present.
bool parseSysDescr(const uint8_t* buf, int len, String& sysDescr) {
    uint8_t tag;
    int cp, cl, np;
    // outer SEQUENCE
    if (!readTL(buf, len, 0, tag, cp, cl, np) || tag != 0x30) return false;
    int p = cp, end = cp + cl;
    // version INTEGER
    if (!readTL(buf, end, p, tag, cp, cl, np) || tag != 0x02) return false;
    p = np;
    // community OCTET STRING
    if (!readTL(buf, end, p, tag, cp, cl, np) || tag != 0x04) return false;
    p = np;
    // PDU (GetResponse = 0xA2)
    if (!readTL(buf, end, p, tag, cp, cl, np)) return false;
    if (tag != 0xA2) return false;
    int pend = cp + cl;
    p = cp;
    // request-id, error-status, error-index (three INTEGERs)
    for (int k = 0; k < 3; k++) {
        if (!readTL(buf, pend, p, tag, cp, cl, np) || tag != 0x02) return false;
        p = np;
    }
    // varbind-list SEQUENCE
    if (!readTL(buf, pend, p, tag, cp, cl, np) || tag != 0x30) return false;
    int vend = cp + cl;
    p = cp;
    // first varbind SEQUENCE
    if (!readTL(buf, vend, p, tag, cp, cl, np) || tag != 0x30) return false;
    int bend = cp + cl;
    p = cp;
    // OID
    if (!readTL(buf, bend, p, tag, cp, cl, np) || tag != 0x06) return false;
    p = np;
    // value
    if (!readTL(buf, bend, p, tag, cp, cl, np)) return false;
    if (tag == 0x04) {  // OCTET STRING = sysDescr text
        String s;
        for (int k = 0; k < cl && k < 120; k++) {
            char c = (char)buf[cp + k];
            s += (c >= 0x20 && c < 0x7F) ? c : '.';  // keep it printable
        }
        sysDescr = s;
    }
    return true;  // a valid response, even if the value wasn't an OCTET STRING
}

}  // namespace

void SnmpSweep::begin(QueueHandle_t outQueue) {
    _mutex = xSemaphoreCreateMutex();
    _outQueue = outQueue;
}

bool SnmpSweep::start() {
    if (_running) {
        notify("snmp sweep already running");
        return false;
    }
    if (_mutex && xSemaphoreTake(_mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
        _responders.clear();
        xSemaphoreGive(_mutex);
    }
    _progressPct = 0;
    _running = true;
    notify(ScanEventType::ScanStarted);
    if (xTaskCreatePinnedToCore(&SnmpSweep::taskEntry, "snmp", 6144, this, 1, nullptr, 0) != pdPASS) {
        _running = false;
        notify(ScanEventType::ScanFinished, 100);
        return false;
    }
    return true;
}

void SnmpSweep::taskEntry(void* arg) {
    static_cast<SnmpSweep*>(arg)->run();
    vTaskDelete(nullptr);
}

void SnmpSweep::run() {
    // Snapshot the alive-host IPs up front, so the sweep works from a
    // stable list even if the host table changes underneath it.
    std::vector<IPAddress> targets;
    size_t n = g_scanManager.hostCount();
    HostInfo h;
    for (size_t i = 0; i < n; i++) {
        if (g_scanManager.getHost(i, h) && h.alive) targets.push_back(h.ip);
    }

    if (targets.empty()) {
        notify("no alive hosts - run NETWORK SCAN first");
        _running = false;
        notify(ScanEventType::ScanFinished, 100);
        return;
    }

    std::vector<uint8_t> req = buildSysDescrGet();

    WiFiUDP udp;
    if (!udp.begin(0)) {
        notify("failed to open UDP socket");
        _running = false;
        notify(ScanEventType::ScanFinished, 100);
        return;
    }

    for (size_t i = 0; i < targets.size() && _running; i++) {
        const IPAddress& ip = targets[i];
        notify("probing " + ip.toString());
        udp.beginPacket(ip, kSnmpPort);
        udp.write(req.data(), req.size());
        udp.endPacket();

        uint32_t start = millis();
        int size = 0;
        while ((millis() - start) < kReplyTimeoutMs) {
            size = udp.parsePacket();
            if (size > 0) break;
            delay(10);
        }
        if (size > 0) {
            uint8_t buf[256];
            int cap = (size > (int)sizeof(buf)) ? (int)sizeof(buf) : size;
            int rl = udp.read(buf, cap);
            String sysDescr;
            if (rl > 0 && parseSysDescr(buf, rl, sysDescr)) {
                addResponder(ip, sysDescr.length() ? sysDescr : String("(no sysDescr)"));
                notify(String("SNMP public: ") + ip.toString());
            }
        }

        _progressPct = (uint8_t)(((i + 1) * 100) / targets.size());
        notify(ScanEventType::ScanProgress, _progressPct);
    }

    udp.stop();
    notify(String((unsigned)count()) + " host(s) answered public");
    _running = false;
    notify(ScanEventType::ScanFinished, 100);
}

void SnmpSweep::addResponder(const IPAddress& ip, const String& sysDescr) {
    if (_mutex && xSemaphoreTake(_mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
        bool dup = false;
        for (auto& r : _responders) {
            if (r.ip == ip) {
                r.sysDescr = sysDescr;
                dup = true;
                break;
            }
        }
        if (!dup && _responders.size() < kMaxResponders) _responders.push_back({ip, sysDescr});
        xSemaphoreGive(_mutex);
    }
}

void SnmpSweep::notify(const String& text) {
    if (!_outQueue) return;
    ScanNotification n;
    n.source = ScanSource::Snmp;
    n.type = ScanEventType::LogLine;
    n.setText(text.c_str());
    xQueueSend(_outQueue, &n, 0);
}

void SnmpSweep::notify(ScanEventType type, uint8_t pct) {
    if (!_outQueue) return;
    ScanNotification n;
    n.source = ScanSource::Snmp;
    n.type = type;
    n.progressPct = pct;
    xQueueSend(_outQueue, &n, 0);
}

size_t SnmpSweep::count() const {
    if (!_mutex || xSemaphoreTake(_mutex, pdMS_TO_TICKS(100)) != pdTRUE) return 0;
    size_t n = _responders.size();
    xSemaphoreGive(_mutex);
    return n;
}

bool SnmpSweep::get(size_t index, Responder& out) const {
    if (!_mutex || xSemaphoreTake(_mutex, pdMS_TO_TICKS(100)) != pdTRUE) return false;
    bool ok = index < _responders.size();
    if (ok) out = _responders[_responders.size() - 1 - index];
    xSemaphoreGive(_mutex);
    return ok;
}
