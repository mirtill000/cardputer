#include "PortScanManager.h"
#include "BannerGrabber.h"
#include "ScanManager.h"
#include "UdpProbe.h"
#include "VulnSignatures.h"
#include "WellKnownHighPorts.h"
#include "../core/Config.h"
#include "../storage/ScanHistory.h"
#include "../storage/SdCard.h"
#include <WiFiClient.h>

PortScanManager g_portScanManager;

void PortScanManager::begin(QueueHandle_t outQueue) {
    _mutex = xSemaphoreCreateMutex();
    _outQueue = outQueue;
}

void PortScanManager::startScan(const IPAddress& target, uint16_t portStart, uint16_t portEnd) {
    if (_running) return;
    if (portEnd < portStart) return;

    // See kMaxRangeSpan in PortScanManager.h: bound the span before
    // allocating _portList so a pathological range can't exhaust internal
    // SRAM mid-scan. The SETTINGS screen warns when a configured range
    // exceeds this, so the cap here is no longer a silent surprise.
    if ((uint32_t)portEnd - (uint32_t)portStart + 1 > kMaxRangeSpan) {
        portEnd = (uint16_t)((uint32_t)portStart + kMaxRangeSpan - 1);
    }

    _portList.clear();
    _portList.reserve((size_t)(portEnd - portStart + 1) + kWellKnownHighPortsCount);
    for (uint32_t p = portStart; p <= portEnd; p++) _portList.push_back((uint16_t)p);
    // Common ports above 1024 (see WellKnownHighPorts.h) that a plain
    // range sweep would otherwise miss entirely - skip any already
    // covered by the configured range instead of probing it twice.
    for (size_t i = 0; i < kWellKnownHighPortsCount; i++) {
        uint16_t p = kWellKnownHighPorts[i];
        if (p < portStart || p > portEnd) _portList.push_back(p);
    }

    if (xSemaphoreTake(_mutex, portMAX_DELAY) == pdTRUE) {
        _openPorts.clear();
        xSemaphoreGive(_mutex);
    }

    _target = target;
    _portStart = portStart;
    _portEnd = portEnd;
    _totalPorts = (uint32_t)_portList.size();
    _portsProbed = 0;
    _progressPct = 0;
    _scanGeneration++;
    _running = true;
    _hasResult = false;

    notify(ScanEventType::ScanStarted);

    uint8_t workerCount = g_config.maxConcurrentProbes;
    if (workerCount < 1) workerCount = 1;
    if (workerCount > 8) workerCount = 8;
    _workersActive = workerCount;

    for (uint8_t i = 0; i < workerCount; i++) {
        auto* args = new WorkerArgs{this, i, workerCount};
        if (xTaskCreatePinnedToCore(&PortScanManager::workerTaskEntry, "portscanw", 6144, args, 1, nullptr, 0) !=
            pdPASS) {
            // Task never created (out of memory): reclaim its args (the
            // entry point that normally deletes them never runs) and
            // account for the worker that will never call
            // onWorkerFinished() itself - identical to ScanManager's
            // handling. If this was the last outstanding worker,
            // onWorkerFinished() finalizes the scan so it never hangs
            // "running" with no task behind it.
            delete args;
            onWorkerFinished();
        }
    }
}

size_t PortScanManager::resultCount() const {
    if (!_mutex || xSemaphoreTake(_mutex, pdMS_TO_TICKS(100)) != pdTRUE) return 0;
    size_t n = _openPorts.size();
    xSemaphoreGive(_mutex);
    return n;
}

bool PortScanManager::getResult(size_t index, PortResult& out) const {
    if (!_mutex || xSemaphoreTake(_mutex, pdMS_TO_TICKS(100)) != pdTRUE) return false;
    bool ok = index < _openPorts.size();
    if (ok) out = _openPorts[index];
    xSemaphoreGive(_mutex);
    return ok;
}

void PortScanManager::workerTaskEntry(void* arg) {
    auto* args = static_cast<WorkerArgs*>(arg);
    PortScanManager* self = args->self;
    uint8_t idx = args->workerIndex;
    uint8_t count = args->workerCount;
    delete args;

    self->runWorker(idx, count);
    vTaskDelete(nullptr);
}

void PortScanManager::runWorker(uint8_t workerIndex, uint8_t workerCount) {
    uint32_t myGeneration = _scanGeneration;
    uint32_t total = _totalPorts;

    for (uint32_t i = workerIndex; i < total; i += workerCount) {
        if (_scanGeneration != myGeneration) break;
        probePort(_portList[i]);
        vTaskDelay(pdMS_TO_TICKS(g_config.interProbeDelayMs));
    }

    onWorkerFinished();
}

void PortScanManager::probePort(uint16_t port) {
    WiFiClient client;
    bool open = client.connect(_target, port, g_config.scanTimeoutMs);

    if (open) {
        PortResult r;
        r.port = port;
        r.open = true;
        BannerGrabber::grab(client, port, g_config.scanTimeoutMs, r);
        client.stop();
        VulnSignatures::check(r.banner, r.vulnNote);

        int16_t newIndex = -1;
        if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
            _openPorts.push_back(r);
            newIndex = (int16_t)(_openPorts.size() - 1);
            xSemaphoreGive(_mutex);
        }

        uint32_t probed = ++_portsProbed;
        uint32_t total = _totalPorts;
        uint8_t pct = total ? (uint8_t)((probed * 100) / total) : 100;
        _progressPct = pct;
        notify(ScanEventType::HostChanged, newIndex, pct);
    } else {
        client.stop();
        uint32_t probed = ++_portsProbed;
        uint32_t total = _totalPorts;
        uint8_t pct = total ? (uint8_t)((probed * 100) / total) : 100;
        _progressPct = pct;
        notify(ScanEventType::ScanProgress, -1, pct);
    }
}

void PortScanManager::onWorkerFinished() {
    if (--_workersActive == 0) {
        _running = false;
        _progressPct = 100;
        _hasResult = true;

        std::vector<PortResult> resultsCopy;
        if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
            resultsCopy = _openPorts;
            xSemaphoreGive(_mutex);
        }

        // A handful of fixed UDP probes (DNS/NTP/SNMP), run once per
        // target after the TCP sweep rather than per-worker - these are
        // only 3 fixed ports total, not worth spreading across the
        // worker pool the way the TCP port range is.
        UdpProbe::probeCommonServices(_target, g_config.scanTimeoutMs, resultsCopy);

        // Marks isNewPort on anything that wasn't open the last time
        // this host was port-scanned, and updates that baseline for
        // next time - see storage/ScanHistory.h.
        ScanHistory::diffAndSavePorts(sdcard::exportFs(), _target, resultsCopy);

        g_scanManager.setHostPorts(_target, resultsCopy);

        notify(ScanEventType::ScanFinished, -1, 100);
    }
}

void PortScanManager::notify(ScanEventType type, int16_t index, uint8_t pct) {
    if (!_outQueue) return;
    ScanNotification n;
    n.source = ScanSource::PortScan;
    n.type = type;
    n.hostIndex = index;
    n.progressPct = pct;
    xQueueSend(_outQueue, &n, 0);
}
