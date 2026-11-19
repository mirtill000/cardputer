#include "DeauthWatcher.h"
#include "../net/PromiscDispatcher.h"
#include "../core/ScanLog.h"
#include "../core/Types.h"
#include "../ui/Sound.h"

DeauthWatcher g_deauthWatcher;

void DeauthWatcher::begin(QueueHandle_t outQueue) {
    _mutex = xSemaphoreCreateMutex();
    _outQueue = outQueue;
    // Same permanent-task-that-idles pattern as CdpLldpSniffer/
    // RogueDhcpDetector - see CdpLldpSniffer.cpp's identical comment.
    xTaskCreatePinnedToCore(&DeauthWatcher::taskEntry, "deauthwatch", 4096, this, 1, nullptr, 0);
}

void DeauthWatcher::start() { _running = true; }
void DeauthWatcher::stop() { _running = false; }

void DeauthWatcher::taskEntry(void* arg) {
    static_cast<DeauthWatcher*>(arg)->run();
}

void DeauthWatcher::run() {
    for (;;) {
        if (!_running) {
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }

        int sub = promisc::subscribe(promisc::kMgmt, &DeauthWatcher::onDispatchedFrame);
        if (sub < 0) {
            notify("radio unavailable - not listening");
            _running = false;
            continue;
        }
        notify("guard mode: watching for deauth floods");
        _windowStartMs = millis();

        while (_running) {
            vTaskDelay(pdMS_TO_TICKS(500));
            if (millis() - _windowStartMs >= kWindowMs) rollWindow();
        }

        promisc::unsubscribe(sub);
        notify("guard mode: stopped");
        // Unlike BeaconProbeSniffer/DeauthManager/PmkidManager, this
        // never changes channel and never leaves promiscuous mode set on
        // its own STA channel - no reconnect nudge needed here.
    }
}

// Runs on the shared promiscuous dispatcher task (net/PromiscDispatcher.h),
// not the WiFi driver task - so the handler may lock/allocate.
void DeauthWatcher::onDispatchedFrame(const promisc::Frame& f) { g_deauthWatcher.onManagementFrame(f.data, f.len); }

void DeauthWatcher::onManagementFrame(const uint8_t* p, uint16_t len) {
    if (len < 24) return;

    uint8_t fc0 = p[0];
    uint8_t type = (fc0 >> 2) & 0x3;
    uint8_t subtype = (fc0 >> 4) & 0xF;
    if (type != 0) return;                          // not Management
    if (subtype != 12 && subtype != 10) return;      // 12 = Deauthentication, 10 = Disassociation

    // Addr3 carries the BSSID regardless of direction (AP->client or
    // client->AP) in an infrastructure BSS - more reliable here than
    // Addr1/Addr2, which swap roles depending on who sent the frame.
    const uint8_t* bssid = p + 16;
    String bssidStr = macToString(bssid);

    _totalFrames++;

    bool becameFlood = false;
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        Incident* existing = nullptr;
        for (auto& inc : _incidents) {
            if (inc.bssid == bssidStr) {
                existing = &inc;
                break;
            }
        }
        if (!existing && _incidents.size() < kMaxIncidents) {
            Incident inc;
            inc.bssid = bssidStr;
            inc.firstSeenMs = millis();
            _incidents.push_back(inc);
            existing = &_incidents.back();
        }
        if (existing) {
            existing->count++;
            existing->windowCount++;
            existing->lastSeenMs = millis();
            if (!existing->flooding && existing->windowCount >= kFloodThreshold) {
                existing->flooding = true;
                becameFlood = true;
                _anyFlooding = true;
            }
        }
        xSemaphoreGive(_mutex);
    }

    if (becameFlood) {
        sound::playCredAlert();  // same urgency tier WardrivingManager uses for a possible evil twin
        notify("deauth flood: " + bssidStr);
    }
}

void DeauthWatcher::rollWindow() {
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        bool anyFlood = false;
        for (auto& inc : _incidents) {
            if (inc.windowCount < kFloodThreshold) inc.flooding = false;  // rate dropped back below threshold
            inc.windowCount = 0;
            if (inc.flooding) anyFlood = true;
        }
        _anyFlooding = anyFlood;
        xSemaphoreGive(_mutex);
    }
    _windowStartMs = millis();
}

void DeauthWatcher::notify(const String& text) { scanlog::post(_outQueue, ScanSource::DeauthWatch, text); }

size_t DeauthWatcher::incidentCount() const {
    if (!_mutex || xSemaphoreTake(_mutex, pdMS_TO_TICKS(100)) != pdTRUE) return 0;
    size_t n = _incidents.size();
    xSemaphoreGive(_mutex);
    return n;
}

bool DeauthWatcher::getIncident(size_t index, Incident& out) const {
    if (!_mutex || xSemaphoreTake(_mutex, pdMS_TO_TICKS(100)) != pdTRUE) return false;
    bool ok = index < _incidents.size();
    if (ok) out = _incidents[_incidents.size() - 1 - index];
    xSemaphoreGive(_mutex);
    return ok;
}
