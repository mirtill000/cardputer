#include "PmkidSweepManager.h"
#include "PmkidManager.h"
#include "WardrivingManager.h"
#include "../net/WifiManager.h"

PmkidSweepManager g_pmkidSweepManager;

void PmkidSweepManager::begin(QueueHandle_t outQueue) {
    _mutex = xSemaphoreCreateMutex();
    _outQueue = outQueue;
    // Permanent-task-that-idles pattern, same as WardrivingManager/
    // SentinelManager - this toggles on/off across possibly many
    // sweeps over a session, not a one-shot run-to-completion task.
    xTaskCreatePinnedToCore(&PmkidSweepManager::taskEntry, "pmkidsweep", 4096, this, 1, nullptr, 0);
}

bool PmkidSweepManager::start() {
    if (_running) return false;
    if (!g_wifi.isConnected()) return false;

    // Snapshot of eligible sightings, taken once - see class comment.
    _targets.clear();
    size_t n = g_wardrivingManager.sightingCount();
    WardrivingManager::ApSighting ap;
    for (size_t i = 0; i < n && _targets.size() < kMaxTargets; i++) {
        if (!g_wardrivingManager.getSighting(i, ap)) continue;
        if (ap.open) continue;              // nothing to capture a PMKID/handshake FOR on an open network
        if (ap.ssid == "<hidden>") continue;  // WiFi.begin() needs a real name to associate to

        Target t;
        t.ssid = ap.ssid;
        t.bssid = ap.bssid;
        t.channel = ap.channel;
        _targets.push_back(t);
    }
    if (_targets.empty()) return false;

    if (xSemaphoreTake(_mutex, portMAX_DELAY) == pdTRUE) {
        _results.clear();
        xSemaphoreGive(_mutex);
    }
    _targetCount = _targets.size();
    _currentIndex = 0;
    _hitCount = 0;

    _running = true;
    return true;
}

void PmkidSweepManager::stop() { _running = false; }

void PmkidSweepManager::taskEntry(void* arg) {
    static_cast<PmkidSweepManager*>(arg)->run();
}

void PmkidSweepManager::run() {
    for (;;) {
        if (!_running) {
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }

        notify("pmkid sweep: " + String((unsigned)_targets.size()) + " targets");

        for (size_t i = 0; i < _targets.size(); i++) {
            _currentIndex = i;
            if (!_running) break;  // stop() was called between targets

            const Target& t = _targets[i];
            if (!g_pmkidManager.start(t.ssid, t.bssid, t.channel)) {
                notify("skip (busy): " + t.ssid);
                continue;
            }

            // PmkidManager's own kCaptureWindowMs bounds this - no
            // separate timeout needed here, and no way to abort it
            // early either, see class comment.
            while (g_pmkidManager.isRunning()) vTaskDelay(pdMS_TO_TICKS(200));

            SweepResult r;
            r.ssid = t.ssid;
            r.bssid = t.bssid;
            r.pmkidCaptured = g_pmkidManager.pmkidLikelyCaptured();
            r.framesCaptured = g_pmkidManager.capturedPackets();
            r.pcapPath = g_pmkidManager.pcapPath();
            addResult(r);
            if (r.pmkidCaptured) _hitCount++;

            notify((r.pmkidCaptured ? "PMKID: " : "no PMKID: ") + t.ssid);
        }

        _currentIndex = _targets.size();  // done - see currentIndex()'s doc comment
        notify("pmkid sweep done: " + String((unsigned)_hitCount) + "/" + String((unsigned)_targets.size()));
        _running = false;
    }
}

void PmkidSweepManager::addResult(const SweepResult& r) {
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
        _results.push_back(r);
        xSemaphoreGive(_mutex);
    }
}

void PmkidSweepManager::notify(const String& text) {
    if (!_outQueue) return;
    ScanNotification n;
    n.source = ScanSource::PmkidSweep;
    n.type = ScanEventType::LogLine;
    n.setText(text.c_str());
    xQueueSend(_outQueue, &n, 0);
}

size_t PmkidSweepManager::resultCount() const {
    if (!_mutex || xSemaphoreTake(_mutex, pdMS_TO_TICKS(100)) != pdTRUE) return 0;
    size_t n = _results.size();
    xSemaphoreGive(_mutex);
    return n;
}

bool PmkidSweepManager::getResult(size_t index, SweepResult& out) const {
    if (!_mutex || xSemaphoreTake(_mutex, pdMS_TO_TICKS(100)) != pdTRUE) return false;
    bool ok = index < _results.size();
    if (ok) out = _results[_results.size() - 1 - index];
    xSemaphoreGive(_mutex);
    return ok;
}
