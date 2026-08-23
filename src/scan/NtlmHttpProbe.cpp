#include "NtlmHttpProbe.h"
#include "Base64.h"
#include "ScanManager.h"
#include "../core/Types.h"
#include "../net/NtlmWire.h"
#include <WiFiClient.h>

NtlmHttpProbe g_ntlmHttpProbe;

namespace {

struct HttpTarget {
    IPAddress ip;
    uint16_t port;
};

// Reads up to maxLen bytes for up to timeoutMs, returning whatever
// arrived (stops early once the peer pauses after sending something) -
// same helper shape as DataStoreProbe::readSome, duplicated locally
// rather than shared since it's a handful of lines (same reasoning as
// WardrivingManager's own small duplicated CSV/MAC helpers).
String readSome(WiFiClient& c, uint16_t timeoutMs, size_t maxLen) {
    String out;
    uint32_t start = millis();
    while ((millis() - start) < timeoutMs && out.length() < maxLen) {
        if (c.available()) {
            out += (char)c.read();
        } else if (out.length() > 0) {
            break;
        } else {
            delay(5);
        }
    }
    return out;
}

// Finds a "WWW-Authenticate: NTLM <token>" header line anywhere in the
// response (case-insensitive on the header name and scheme) and returns
// the token. A response can legally carry several WWW-Authenticate lines
// (one per scheme it supports, e.g. Negotiate/NTLM/Basic each on their
// own line) - every occurrence is checked, not just the first. Returns
// "" if NTLM wasn't offered at all, or was mentioned with no token
// attached (the initial-advertisement shape, not expected here since
// this module always sends its own NEGOTIATE_MESSAGE first).
String extractNtlmToken(const String& response) {
    String lower = response;
    lower.toLowerCase();

    int p = lower.indexOf("www-authenticate:");
    while (p >= 0) {
        int lineEnd = response.indexOf('\n', p);
        if (lineEnd < 0) lineEnd = response.length();
        String line = response.substring(p, lineEnd);
        line.trim();

        int colon = line.indexOf(':');
        if (colon >= 0) {
            String value = line.substring(colon + 1);
            value.trim();
            String valueLower = value;
            valueLower.toLowerCase();
            if (valueLower.startsWith("ntlm")) {
                String token = value.substring(4);
                token.trim();
                if (token.length() > 0) return token;
            }
        }

        p = lower.indexOf("www-authenticate:", p + 1);
    }
    return "";
}

}  // namespace

void NtlmHttpProbe::begin(QueueHandle_t outQueue) {
    _mutex = xSemaphoreCreateMutex();
    _outQueue = outQueue;
}

bool NtlmHttpProbe::start() {
    if (_running) {
        notify("ntlm disclosure already running");
        return false;
    }
    if (_mutex && xSemaphoreTake(_mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
        _findings.clear();
        xSemaphoreGive(_mutex);
    }
    _progressPct = 0;
    _running = true;
    notify(ScanEventType::ScanStarted);
    if (xTaskCreatePinnedToCore(&NtlmHttpProbe::taskEntry, "ntlmhttp", 6144, this, 1, nullptr, 0) != pdPASS) {
        _running = false;
        notify(ScanEventType::ScanFinished, 100);
        return false;
    }
    return true;
}

void NtlmHttpProbe::taskEntry(void* arg) {
    static_cast<NtlmHttpProbe*>(arg)->run();
    vTaskDelete(nullptr);
}

void NtlmHttpProbe::run() {
    std::vector<HttpTarget> targets;
    size_t n = g_scanManager.hostCount();
    HostInfo h;
    for (size_t i = 0; i < n; i++) {
        if (!g_scanManager.getHost(i, h) || !h.alive) continue;
        for (const auto& p : h.ports) {
            if (p.service == "http") {
                targets.push_back({h.ip, p.port});
                break;  // one HTTP port per host is enough for this check
            }
        }
    }

    if (targets.empty()) {
        notify("no HTTP hosts - run NETWORK SCAN/PORT SCAN first");
        _running = false;
        notify(ScanEventType::ScanFinished, 100);
        return;
    }

    for (size_t i = 0; i < targets.size() && _running; i++) {
        notify("probing " + targets[i].ip.toString());
        probeHost(targets[i].ip, targets[i].port);
        _progressPct = (uint8_t)(((i + 1) * 100) / targets.size());
        notify(ScanEventType::ScanProgress, _progressPct);
    }

    notify(String((unsigned)count()) + " NTLM responder(s)");
    _running = false;
    notify(ScanEventType::ScanFinished, 100);
}

void NtlmHttpProbe::probeHost(const IPAddress& ip, uint16_t port) {
    WiFiClient c;
    if (!c.connect(ip, port, kConnectTimeoutMs)) return;

    std::vector<uint8_t> type1 = ntlmwire::buildType1Negotiate();
    String b64Type1 = b64::encode(type1.data(), type1.size());

    String req = "GET / HTTP/1.1\r\n";
    req += "Host: " + ip.toString() + "\r\n";
    req += "Authorization: NTLM " + b64Type1 + "\r\n";
    req += "Connection: close\r\n\r\n";
    c.print(req);

    String resp = readSome(c, kReadTimeoutMs, 3072);
    c.stop();

    String token = extractNtlmToken(resp);
    if (token.isEmpty()) return;  // NTLM not offered here - not a finding

    std::vector<uint8_t> raw = b64::decode(token);
    if (raw.empty()) return;

    ntlmwire::Type2Info info;
    if (!ntlmwire::parseType2Challenge(raw.data(), raw.size(), info)) return;

    Finding f;
    f.ip = ip;
    f.port = port;
    f.netbiosDomain = info.netbiosDomain;
    f.netbiosComputer = info.netbiosComputer;
    f.dnsDomain = info.dnsDomain;
    f.dnsComputer = info.dnsComputer;
    addFinding(f);
}

void NtlmHttpProbe::addFinding(const Finding& f) {
    if (_mutex && xSemaphoreTake(_mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
        if (_findings.size() < kMaxFindings) _findings.push_back(f);
        xSemaphoreGive(_mutex);
    }

    String msg = "NTLM @ " + f.ip.toString();
    if (f.dnsDomain.length()) {
        msg += " domain=" + f.dnsDomain;
    } else if (f.netbiosDomain.length()) {
        msg += " domain=" + f.netbiosDomain;
    }
    notify(msg);
}

void NtlmHttpProbe::notify(const String& text) {
    if (!_outQueue) return;
    ScanNotification n;
    n.source = ScanSource::NtlmHttp;
    n.type = ScanEventType::LogLine;
    n.setText(text.c_str());
    xQueueSend(_outQueue, &n, 0);
}

void NtlmHttpProbe::notify(ScanEventType type, uint8_t pct) {
    if (!_outQueue) return;
    ScanNotification n;
    n.source = ScanSource::NtlmHttp;
    n.type = type;
    n.progressPct = pct;
    xQueueSend(_outQueue, &n, 0);
}

size_t NtlmHttpProbe::count() const {
    if (!_mutex || xSemaphoreTake(_mutex, pdMS_TO_TICKS(100)) != pdTRUE) return 0;
    size_t n = _findings.size();
    xSemaphoreGive(_mutex);
    return n;
}

bool NtlmHttpProbe::get(size_t index, Finding& out) const {
    if (!_mutex || xSemaphoreTake(_mutex, pdMS_TO_TICKS(100)) != pdTRUE) return false;
    bool ok = index < _findings.size();
    if (ok) out = _findings[_findings.size() - 1 - index];
    xSemaphoreGive(_mutex);
    return ok;
}
