#include "HttpPathBruteforcer.h"
#include "../core/Config.h"
#include <WiFiClient.h>

HttpPathBruteforcer g_httpBruteforcer;

namespace {

// Small, deliberately built-in list — no wordlist file on
// LittleFS/SD to manage for this one, unlike CredAuditManager's
// user/password lists. Common config/backup/VCS-leak/admin paths that
// show up in real-world misconfig scans; not trying to be exhaustive
// (that's what a real dirb/gobuster run from a PC is for).
constexpr const char* kPaths[] = {
    "/admin", "/administrator", "/login", "/wp-admin", "/wp-login.php", "/phpmyadmin", "/.git/config",
    "/.git/HEAD", "/.env", "/.svn/entries", "/backup", "/backup.zip", "/backup.tar.gz", "/config.php",
    "/config.json", "/server-status", "/server-info", "/.htaccess", "/.ssh/id_rsa", "/api", "/api/v1",
    "/debug", "/console", "/actuator", "/actuator/health", "/swagger.json", "/robots.txt", "/.well-known/security.txt",
};
constexpr size_t kPathCount = sizeof(kPaths) / sizeof(kPaths[0]);

String readStatusLine(WiFiClient& client, uint16_t timeoutMs) {
    String out;
    uint32_t start = millis();
    while ((millis() - start) < timeoutMs) {
        if (client.available()) {
            int c = client.read();
            if (c < 0 || c == '\n') break;
            if (c != '\r') out += (char)c;
        } else {
            delay(5);
        }
    }
    return out;
}

}  // namespace

void HttpPathBruteforcer::begin(QueueHandle_t outQueue) {
    _mutex = xSemaphoreCreateMutex();
    _outQueue = outQueue;
}

void HttpPathBruteforcer::start(const IPAddress& target, uint16_t port) {
    if (_running) {
        // Already probing - tell the user instead of silently doing nothing.
        ScanNotification busy;
        busy.source = ScanSource::HttpBrute;
        busy.type = ScanEventType::LogLine;
        busy.setText("path brute already running");
        if (_outQueue) xQueueSend(_outQueue, &busy, 0);
        return;
    }
    _target = target;
    _port = port;
    _tried = 0;
    if (_mutex && xSemaphoreTake(_mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
        _hits.clear();
        xSemaphoreGive(_mutex);
    }

    _running = true;
    notify(ScanEventType::ScanStarted);
    if (xTaskCreatePinnedToCore(&HttpPathBruteforcer::taskEntry, "httpbrute", 4096, this, 1, nullptr, 0) != pdPASS) {
        _running = false;
        ScanNotification fail;
        fail.source = ScanSource::HttpBrute;
        fail.type = ScanEventType::LogLine;
        fail.setText("failed to start path brute task");
        if (_outQueue) xQueueSend(_outQueue, &fail, 0);
        notify(ScanEventType::ScanFinished, 100);
    }
}

void HttpPathBruteforcer::taskEntry(void* arg) {
    static_cast<HttpPathBruteforcer*>(arg)->run();
    vTaskDelete(nullptr);
}

void HttpPathBruteforcer::run() {
    for (size_t i = 0; i < kPathCount; i++) {
        uint16_t status = 0;
        if (tryPath(kPaths[i], status)) {
            if (status != 404) logHit(kPaths[i], status);
        }
        _tried++;
        notify(ScanEventType::ScanProgress, (uint8_t)((_tried * 100) / kPathCount));
        vTaskDelay(pdMS_TO_TICKS(g_config.interProbeDelayMs));
    }

    _running = false;
    notify(ScanEventType::ScanFinished, 100);
}

bool HttpPathBruteforcer::tryPath(const String& path, uint16_t& statusOut) {
    WiFiClient client;
    if (!client.connect(_target, _port, g_config.scanTimeoutMs)) return false;

    client.print("GET ");
    client.print(path);
    client.print(" HTTP/1.0\r\nHost: scan\r\nConnection: close\r\n\r\n");

    String statusLine = readStatusLine(client, g_config.scanTimeoutMs);
    client.stop();

    // "HTTP/1.x NNN ..." — pull the 3-digit code out of a fixed offset
    // rather than a general parser; every server that speaks HTTP at
    // all sends the status line in exactly this shape.
    int spaceIdx = statusLine.indexOf(' ');
    if (spaceIdx < 0 || (int)statusLine.length() < spaceIdx + 4) return false;
    statusOut = (uint16_t)statusLine.substring(spaceIdx + 1, spaceIdx + 4).toInt();
    return statusOut > 0;
}

void HttpPathBruteforcer::logHit(const String& path, uint16_t status) {
    Hit h;
    h.path = path;
    h.status = status;
    if (_mutex && xSemaphoreTake(_mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
        _hits.push_back(h);
        xSemaphoreGive(_mutex);
    }

    ScanNotification n;
    n.source = ScanSource::HttpBrute;
    n.type = ScanEventType::LogLine;
    n.setText((path + " -> " + String(status)).c_str());
    if (_outQueue) xQueueSend(_outQueue, &n, 0);
}

void HttpPathBruteforcer::notify(ScanEventType type, uint8_t pct) {
    if (!_outQueue) return;
    ScanNotification n;
    n.source = ScanSource::HttpBrute;
    n.type = type;
    n.progressPct = pct;
    xQueueSend(_outQueue, &n, 0);
}

size_t HttpPathBruteforcer::hitCount() const {
    if (!_mutex || xSemaphoreTake(_mutex, pdMS_TO_TICKS(100)) != pdTRUE) return 0;
    size_t n = _hits.size();
    xSemaphoreGive(_mutex);
    return n;
}

bool HttpPathBruteforcer::getHit(size_t index, Hit& out) const {
    if (!_mutex || xSemaphoreTake(_mutex, pdMS_TO_TICKS(100)) != pdTRUE) return false;
    bool ok = index < _hits.size();
    if (ok) out = _hits[_hits.size() - 1 - index];
    xSemaphoreGive(_mutex);
    return ok;
}
