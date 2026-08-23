#include "IotCredScanner.h"
#include "CredAuditManager.h"
#include "IotDefaultCreds.h"
#include "ScanManager.h"
#include "../core/Config.h"
#include "../core/Types.h"
#include "../ui/Sound.h"
#include <cstring>

IotCredScanner g_iotCredScanner;

namespace {
bool isHttpPort(const PortResult& p) {
    if (!p.open || p.isUdp) return false;
    if (p.service == "http") return true;
    return p.port == 80 || p.port == 8080 || p.port == 8000 || p.port == 8443;
}
}  // namespace

void IotCredScanner::begin(QueueHandle_t outQueue) {
    _mutex = xSemaphoreCreateMutex();
    _outQueue = outQueue;
}

bool IotCredScanner::start() {
    if (_running) return false;
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
        _hits.clear();
        xSemaphoreGive(_mutex);
    }
    _attempts = 0;
    _running = true;
    notify(ScanEventType::ScanStarted);
    xTaskCreatePinnedToCore(&IotCredScanner::taskEntry, "iotcred", 6144, this, 1, nullptr, 0);
    return true;
}

void IotCredScanner::stop() { _running = false; }

void IotCredScanner::taskEntry(void* arg) {
    static_cast<IotCredScanner*>(arg)->run();
    vTaskDelete(nullptr);
}

void IotCredScanner::sweepHost(const IPAddress& ip, uint16_t httpPort, bool hasTelnet,
                               const String& fingerprint) {
    bool httpHit = false, telnetHit = false;

    for (size_t i = 0; i < IotDefaultCreds::kCount && _running; i++) {
        const IotCredential& c = IotDefaultCreds::kEntries[i];
        bool generic = (c.keyword[0] == '\0');
        bool matches = generic || (fingerprint.indexOf(c.keyword) >= 0);
        if (!matches) continue;

        bool isHttp = (strcmp(c.service, "http") == 0);
        if (isHttp) {
            if (httpPort == 0 || httpHit) continue;
        } else {  // telnet
            if (!hasTelnet || telnetHit) continue;
        }

        _attempts++;
        bool ok = g_credAuditManager.tryLogin(c.service, ip, isHttp ? httpPort : 23, c.user, c.pass);
        if (ok) {
            Hit h;
            h.ip = ip;
            h.service = c.service;
            h.user = c.user;
            h.pass = c.pass;
            h.device = generic ? String("generic") : String(c.keyword);
            addHit(h);
            if (isHttp) httpHit = true; else telnetHit = true;
        }
        vTaskDelay(pdMS_TO_TICKS(g_config.interProbeDelayMs));
        if (httpHit && telnetHit) break;
        if (httpHit && !hasTelnet) break;
        if (telnetHit && httpPort == 0) break;
    }
}

void IotCredScanner::run() {
    size_t hosts = g_scanManager.hostCount();
    HostInfo host;
    size_t swept = 0;

    for (size_t i = 0; i < hosts && _running && swept < kMaxHosts; i++) {
        if (!g_scanManager.getHost(i, host) || !host.alive) continue;

        uint16_t httpPort = 0;
        bool hasTelnet = false;
        String fp = host.vendor;
        for (const auto& p : host.ports) {
            if (isHttpPort(p) && httpPort == 0) httpPort = p.port;
            if (p.open && !p.isUdp && (p.port == 23 || p.service == "telnet")) hasTelnet = true;
            if (p.banner.length()) fp += " " + p.banner;
            if (p.service.length()) fp += " " + p.service;
        }
        if (httpPort == 0 && !hasTelnet) continue;

        fp.toLowerCase();
        notify(host.ip.toString() + " ...");
        sweepHost(host.ip, httpPort, hasTelnet, fp);
        swept++;
    }

    if (count() > 0) sound::playCredAlert();
    notify(String((unsigned)swept) + " host(s), " + String((unsigned)count()) + " default cred(s)");
    _running = false;
    notify(ScanEventType::ScanFinished, 100);
}

void IotCredScanner::addHit(const Hit& h) {
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
        _hits.push_back(h);
        xSemaphoreGive(_mutex);
    }
    notify(h.ip.toString() + " " + h.service + " " + h.user + "/" +
           (h.pass.length() ? h.pass : String("<blank>")));
}

void IotCredScanner::notify(const String& text) {
    if (!_outQueue) return;
    ScanNotification n;
    n.source = ScanSource::IotCred;
    n.type = ScanEventType::LogLine;
    n.setText(text.c_str());
    xQueueSend(_outQueue, &n, 0);
}

void IotCredScanner::notify(ScanEventType type, uint8_t pct) {
    if (!_outQueue) return;
    ScanNotification n;
    n.source = ScanSource::IotCred;
    n.type = type;
    n.progressPct = pct;
    xQueueSend(_outQueue, &n, 0);
}

size_t IotCredScanner::count() const {
    if (!_mutex || xSemaphoreTake(_mutex, pdMS_TO_TICKS(100)) != pdTRUE) return 0;
    size_t n = _hits.size();
    xSemaphoreGive(_mutex);
    return n;
}

bool IotCredScanner::get(size_t index, Hit& out) const {
    if (!_mutex || xSemaphoreTake(_mutex, pdMS_TO_TICKS(100)) != pdTRUE) return false;
    bool ok = index < _hits.size();
    if (ok) out = _hits[_hits.size() - 1 - index];
    xSemaphoreGive(_mutex);
    return ok;
}
