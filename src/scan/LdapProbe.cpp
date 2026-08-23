#include "LdapProbe.h"
#include "ScanManager.h"
#include "../core/Types.h"
#include "../net/LdapWire.h"
#include <WiFiClient.h>

LdapProbe g_ldapProbe;

namespace {

// Binary-safe bounded read (unlike DataStoreProbe::readSome, which
// appends into a String - fine for the text-ish protocols it reads, but
// this module needs indexed byte access for BER parsing). Stops early
// once the peer pauses after sending something, same heuristic as
// DataStoreProbe's reader.
size_t readBytes(WiFiClient& c, uint8_t* buf, size_t maxLen, uint32_t timeoutMs) {
    size_t got = 0;
    uint32_t start = millis();
    while ((millis() - start) < timeoutMs && got < maxLen) {
        if (c.available()) {
            int n = c.read(buf + got, maxLen - got);
            if (n > 0) got += (size_t)n;
        } else if (got > 0) {
            break;
        } else {
            delay(5);
        }
    }
    return got;
}

}  // namespace

void LdapProbe::begin(QueueHandle_t outQueue) {
    _mutex = xSemaphoreCreateMutex();
    _outQueue = outQueue;
}

bool LdapProbe::start() {
    if (_running) {
        notify("ldap sweep already running");
        return false;
    }
    if (_mutex && xSemaphoreTake(_mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
        _findings.clear();
        xSemaphoreGive(_mutex);
    }
    _progressPct = 0;
    _running = true;
    notify(ScanEventType::ScanStarted);
    if (xTaskCreatePinnedToCore(&LdapProbe::taskEntry, "ldap", 6144, this, 1, nullptr, 0) != pdPASS) {
        _running = false;
        notify(ScanEventType::ScanFinished, 100);
        return false;
    }
    return true;
}

void LdapProbe::taskEntry(void* arg) {
    static_cast<LdapProbe*>(arg)->run();
    vTaskDelete(nullptr);
}

void LdapProbe::run() {
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

    for (size_t i = 0; i < targets.size() && _running; i++) {
        notify("probing " + targets[i].toString());
        probeHost(targets[i]);
        _progressPct = (uint8_t)(((i + 1) * 100) / targets.size());
        notify(ScanEventType::ScanProgress, _progressPct);
    }

    notify(String((unsigned)count()) + " LDAP responder(s)");
    _running = false;
    notify(ScanEventType::ScanFinished, 100);
}

void LdapProbe::probeHost(const IPAddress& ip) {
    WiFiClient c;
    if (!c.connect(ip, kLdapPort, kConnectTimeoutMs)) return;  // nothing on 389 - most hosts, not a finding

    std::vector<uint8_t> bindReq = ldapwire::buildAnonymousBind();
    c.write(bindReq.data(), bindReq.size());

    uint8_t buf[512];
    size_t n = readBytes(c, buf, sizeof(buf), kReadTimeoutMs);

    bool bindOk = false;
    // A false return here means "didn't parse as an LDAP BindResponse at
    // all" - something else is listening on 389 (or nothing readable
    // came back) - not the same as a REJECTED bind, which parses fine
    // with bindOk=false. Only the former is skipped as "not really LDAP".
    if (n == 0 || !ldapwire::parseBindResponse(buf, n, bindOk)) {
        c.stop();
        return;
    }

    Finding f;
    f.ip = ip;
    f.anonymousBindAllowed = bindOk;

    // rootDSE search on the SAME connection, regardless of the bind
    // result above - RFC 4511 §5.1 means this is meant to work even
    // unauthenticated, and plenty of real servers still honor that even
    // when general anonymous bind is rejected (see class comment).
    std::vector<uint8_t> searchReq = ldapwire::buildRootDseSearch();
    c.write(searchReq.data(), searchReq.size());

    uint8_t sbuf[900];
    size_t sn = readBytes(c, sbuf, sizeof(sbuf), kReadTimeoutMs);
    if (sn > 0) {
        std::vector<String> wanted = {"namingContexts", "defaultNamingContext", "dnsHostName"};
        std::vector<String> values(wanted.size());
        if (ldapwire::parseSearchResultEntry(sbuf, sn, wanted, values)) {
            f.namingContexts = values[0];
            f.defaultNamingContext = values[1];
            f.dnsHostName = values[2];
        }
        // If it doesn't parse as an entry (e.g. it's straight to
        // SearchResultDone with zero entries - rootDSE locked down),
        // `f` simply keeps its empty rootDSE fields - still a valid,
        // informative finding (anonymous bind result alone).
    }

    c.stop();
    addFinding(f);
}

void LdapProbe::addFinding(const Finding& f) {
    if (_mutex && xSemaphoreTake(_mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
        if (_findings.size() < kMaxFindings) _findings.push_back(f);
        xSemaphoreGive(_mutex);
    }

    String msg = String(f.anonymousBindAllowed ? "OPEN LDAP bind @ " : "LDAP @ ") + f.ip.toString();
    if (f.dnsHostName.length()) msg += " (" + f.dnsHostName + ")";
    notify(msg);
}

void LdapProbe::notify(const String& text) {
    if (!_outQueue) return;
    ScanNotification n;
    n.source = ScanSource::Ldap;
    n.type = ScanEventType::LogLine;
    n.setText(text.c_str());
    xQueueSend(_outQueue, &n, 0);
}

void LdapProbe::notify(ScanEventType type, uint8_t pct) {
    if (!_outQueue) return;
    ScanNotification n;
    n.source = ScanSource::Ldap;
    n.type = type;
    n.progressPct = pct;
    xQueueSend(_outQueue, &n, 0);
}

size_t LdapProbe::count() const {
    if (!_mutex || xSemaphoreTake(_mutex, pdMS_TO_TICKS(100)) != pdTRUE) return 0;
    size_t n = _findings.size();
    xSemaphoreGive(_mutex);
    return n;
}

bool LdapProbe::get(size_t index, Finding& out) const {
    if (!_mutex || xSemaphoreTake(_mutex, pdMS_TO_TICKS(100)) != pdTRUE) return false;
    bool ok = index < _findings.size();
    if (ok) out = _findings[_findings.size() - 1 - index];
    xSemaphoreGive(_mutex);
    return ok;
}
