#include "BleScanManager.h"
#include "../net/TimeSync.h"
#include "../storage/SdCard.h"
#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>

BleScanManager g_bleScanManager;

namespace {

// Duplicated small CSV-field escaper - same shape as the one in
// WardrivingManager.cpp (which itself duplicated ResultStore's), now a
// third copy. Still just four lines; not worth a shared header for it.
void csvField(String s, String& out) {
    bool needsQuotes = s.indexOf(',') >= 0 || s.indexOf('"') >= 0 || s.indexOf('\n') >= 0;
    s.replace("\"", "\"\"");
    if (needsQuotes) {
        out += '"';
        out += s;
        out += '"';
    } else {
        out += s;
    }
}

}  // namespace

void BleScanManager::begin(QueueHandle_t outQueue) {
    _mutex = xSemaphoreCreateMutex();
    _outQueue = outQueue;
    // Same permanent-task-that-idles pattern as WardrivingManager::begin -
    // see its comment for why this isn't created/destroyed per start()/stop().
    xTaskCreatePinnedToCore(&BleScanManager::taskEntry, "blescan", 8192, this, 1, nullptr, 0);
}

void BleScanManager::start() { _running = true; }
void BleScanManager::stop() { _running = false; }

void BleScanManager::taskEntry(void* arg) {
    static_cast<BleScanManager*>(arg)->run();
}

void BleScanManager::run() {
    for (;;) {
        if (!_running) {
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }

        runScanCycle();

        for (uint32_t waited = 0; waited < kScanIntervalMs && _running; waited += 500) {
            vTaskDelay(pdMS_TO_TICKS(500));
        }
    }
}

void BleScanManager::runScanCycle() {
    // Lazy on purpose - BLEDevice::init() pulls in the BLE stack (RAM +
    // startup cost) that a user who never opens this screen shouldn't pay
    // for. Once started, left initialized for the rest of the session
    // rather than torn down on stop() - BLEDevice::deinit() has its own
    // sharp edges and this firmware only ever starts BLE scanning once
    // per boot in practice.
    if (!_bleInitialized) {
        BLEDevice::init("");
        _bleInitialized = true;
    }

    BLEScan* scan = BLEDevice::getScan();
    scan->setActiveScan(true);
    // Blocking: returns once `kBleScanSeconds` of scanning has elapsed.
    // Returned BY VALUE on the arduino-esp32 core version this project
    // actually builds against (confirmed on real hardware - the pointer
    // variant assumed here originally, matching a different core
    // release, failed to compile). See BleScanManager.h's RISK comment.
    BLEScanResults results = scan->start(kBleScanSeconds, false);
    int count = results.getCount();

    for (int i = 0; i < count; i++) {
        BLEAdvertisedDevice dev = results.getDevice((uint32_t)i);

        BleSighting rec;
        rec.address = dev.getAddress().toString();
        rec.name = dev.haveName() ? dev.getName() : "";
        rec.rssi = dev.getRSSI();
        if (rec.address.isEmpty()) continue;

        bool isNew = false;
        if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
            BleSighting* existing = nullptr;
            for (auto& s : _sightings) {
                if (s.address == rec.address) {
                    existing = &s;
                    break;
                }
            }
            if (existing) {
                existing->rssi = rec.rssi;
                existing->lastSeenMs = millis();
                // A device only sometimes advertises its name - keep the
                // first one seen rather than overwriting with a blank.
                if (existing->name.isEmpty() && rec.name.length()) existing->name = rec.name;
            } else if (_sightings.size() < kMaxSightings) {
                rec.firstSeenMs = millis();
                rec.lastSeenMs = rec.firstSeenMs;
                _sightings.push_back(rec);
                isNew = true;
            }
            xSemaphoreGive(_mutex);
        }

        if (isNew) logSighting(rec);
    }

    // Frees BLEScanResults' internal device map - without this it keeps
    // growing across every future cycle for the rest of the session.
    scan->clearResults();
}

void BleScanManager::logSighting(const BleSighting& d) {
    fs::FS& fs = sdcard::exportFs();
    fs.mkdir("/blescan");

    bool isNewFile = !fs.exists("/blescan/blescan.csv");
    File f = fs.open("/blescan/blescan.csv", "a");
    if (!f) return;
    if (isNewFile) f.println("time,address,name,rssi");

    String t = TimeSync::isSynced() ? TimeSync::nowString() : ("uptime:" + String(millis() / 1000));

    String row;
    csvField(t, row);
    row += ',';
    csvField(d.address, row);
    row += ',';
    csvField(d.name, row);
    row += ',';
    row += String(d.rssi);

    f.println(row);
    f.close();

    notify("BLE: " + (d.name.length() ? d.name : d.address));
}

void BleScanManager::notify(const String& text) {
    if (!_outQueue) return;
    ScanNotification n;
    n.source = ScanSource::Ble;
    n.type = ScanEventType::LogLine;
    n.setText(text.c_str());
    xQueueSend(_outQueue, &n, 0);
}

size_t BleScanManager::sightingCount() const {
    if (!_mutex || xSemaphoreTake(_mutex, pdMS_TO_TICKS(100)) != pdTRUE) return 0;
    size_t n = _sightings.size();
    xSemaphoreGive(_mutex);
    return n;
}

bool BleScanManager::getSighting(size_t index, BleSighting& out) const {
    if (!_mutex || xSemaphoreTake(_mutex, pdMS_TO_TICKS(100)) != pdTRUE) return false;
    // Most-recently-seen first for display - _sightings is in discovery
    // order (oldest first), so index from the back.
    bool ok = index < _sightings.size();
    if (ok) out = _sightings[_sightings.size() - 1 - index];
    xSemaphoreGive(_mutex);
    return ok;
}
