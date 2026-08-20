#include "PasswordSprayManager.h"
#include "CredAuditManager.h"
#include "ScanManager.h"
#include "WordlistLoader.h"
#include "../core/Config.h"
#include "../core/Types.h"
#include "../ui/Sound.h"

PasswordSprayManager g_passwordSpray;

namespace {
// Small, well-known enterprise-typical username list. Kept short on
// purpose — spray success comes from breadth (many hosts), not depth
// (many users on one).
const char* kSprayUsers[] = {
    "admin",  "administrator", "root",    "user",     "guest",
    "test",   "operator",      "manager", "service",  "support",
};
constexpr size_t kSprayUserCount = sizeof(kSprayUsers) / sizeof(kSprayUsers[0]);

// The set of TCP services CredAuditManager::tryLogin() handles.
struct SprayService { const char* name; uint16_t port; };
const SprayService kServices[] = {
    {"http", 80}, {"telnet", 23}, {"ftp", 21}, {"pop3", 110}, {"imap", 143}, {"smtp", 25},
};
constexpr size_t kServiceCount = sizeof(kServices) / sizeof(kServices[0]);

bool servicePortOpen(const HostInfo& h, const SprayService& s, uint16_t& portOut) {
    for (const auto& p : h.ports) {
        if (!p.open || p.isUdp) continue;
        // Match by service string when available (real port from banner
        // grab), else by the well-known port fallback.
        if (p.service == s.name || p.port == s.port) { portOut = p.port; return true; }
    }
    return false;
}
}  // namespace

void PasswordSprayManager::begin(QueueHandle_t outQueue) {
    _mutex = xSemaphoreCreateMutex();
    _outQueue = outQueue;
}

bool PasswordSprayManager::start(const String& password) {
    if (_running || password.isEmpty()) return false;
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
        _hits.clear();
        xSemaphoreGive(_mutex);
    }
    _password = password;
    _attempts = 0;
    _targets = 0;
    _running = true;
    notify(ScanEventType::ScanStarted);
    xTaskCreatePinnedToCore(&PasswordSprayManager::taskEntry, "spray", 6144, this, 1, nullptr, 0);
    return true;
}

void PasswordSprayManager::stop() { _running = false; }

void PasswordSprayManager::taskEntry(void* arg) {
    static_cast<PasswordSprayManager*>(arg)->run();
    vTaskDelete(nullptr);
}

void PasswordSprayManager::tryOnHost(const IPAddress& ip, const char* service, uint16_t port,
                                     const std::vector<String>& users) {
    for (const auto& u : users) {
        if (!_running) return;
        _attempts++;
        bool ok = g_credAuditManager.tryLogin(service, ip, port, u, _password);
        notify(String(service) + " " + ip.toString() + " " + u + (ok ? " OK" : ""));
        if (ok) {
            Hit h;
            h.ip = ip;
            h.service = service;
            h.port = port;
            h.user = u;
            addHit(h);
            // Move on to the next service on this host - a successful spray
            // hit is enough per (host, service); no need to keep spraying
            // other users, they'd be redundant noise.
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(kInterAttemptDelayMs));
    }
}

void PasswordSprayManager::run() {
    // Build the user list: built-in + optional SD wordlist. Dedup, cap.
    std::vector<String> users;
    for (size_t i = 0; i < kSprayUserCount; i++) users.push_back(String(kSprayUsers[i]));
    std::vector<String> extra = WordlistLoader::load("/creds/users.txt", 40);
    for (const auto& e : extra) {
        bool dup = false;
        for (const auto& u : users) if (u == e) { dup = true; break; }
        if (!dup) users.push_back(e);
    }

    notify(String((unsigned)users.size()) + " users, pw='" + _password + "'");

    // Collect (host, service, port) triples first so progress reporting can
    // be meaningful and hosts get sprayed in a predictable order.
    struct Target { IPAddress ip; const SprayService* svc; uint16_t port; };
    std::vector<Target> targets;
    size_t hosts = g_scanManager.hostCount();
    HostInfo h;
    for (size_t i = 0; i < hosts && targets.size() < kMaxHosts * kServiceCount; i++) {
        if (!g_scanManager.getHost(i, h) || !h.alive) continue;
        for (size_t s = 0; s < kServiceCount; s++) {
            uint16_t port = 0;
            if (servicePortOpen(h, kServices[s], port)) {
                Target t{h.ip, &kServices[s], port};
                targets.push_back(t);
            }
        }
    }
    _targets = (uint32_t)targets.size();

    if (targets.empty()) {
        notify("no HTTP/Telnet/FTP/POP3/IMAP/SMTP targets");
        _running = false;
        notify(ScanEventType::ScanFinished, 100);
        return;
    }

    for (size_t i = 0; i < targets.size() && _running; i++) {
        const Target& t = targets[i];
        tryOnHost(t.ip, t.svc->name, t.port, users);
        notify(ScanEventType::ScanProgress, (uint8_t)((i + 1) * 100 / targets.size()));
    }

    if (count() > 0) sound::playCredAlert();
    notify(String((unsigned)_attempts) + " attempts, " + String((unsigned)count()) + " hit(s)");
    _running = false;
    notify(ScanEventType::ScanFinished, 100);
}

void PasswordSprayManager::addHit(const Hit& h) {
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
        if (_hits.size() < kMaxHits) _hits.push_back(h);
        xSemaphoreGive(_mutex);
    }
}

void PasswordSprayManager::notify(const String& text) {
    if (!_outQueue) return;
    ScanNotification n;
    n.source = ScanSource::Spray;
    n.type = ScanEventType::LogLine;
    n.setText(text.c_str());
    xQueueSend(_outQueue, &n, 0);
}

void PasswordSprayManager::notify(ScanEventType type, uint8_t pct) {
    if (!_outQueue) return;
    ScanNotification n;
    n.source = ScanSource::Spray;
    n.type = type;
    n.progressPct = pct;
    xQueueSend(_outQueue, &n, 0);
}

size_t PasswordSprayManager::count() const {
    if (!_mutex || xSemaphoreTake(_mutex, pdMS_TO_TICKS(100)) != pdTRUE) return 0;
    size_t n = _hits.size();
    xSemaphoreGive(_mutex);
    return n;
}

bool PasswordSprayManager::get(size_t index, Hit& out) const {
    if (!_mutex || xSemaphoreTake(_mutex, pdMS_TO_TICKS(100)) != pdTRUE) return false;
    bool ok = index < _hits.size();
    if (ok) out = _hits[_hits.size() - 1 - index];
    xSemaphoreGive(_mutex);
    return ok;
}
