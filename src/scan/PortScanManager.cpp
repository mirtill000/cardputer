#include "PortScanManager.h"
#include "BannerGrabber.h"
#include "ScanManager.h"
#include "../core/Config.h"
#include <WiFiClient.h>

PortScanManager g_portScanManager;

void PortScanManager::begin(QueueHandle_t outQueue) {
    _mutex = xSemaphoreCreateMutex();
    _outQueue = outQueue;
}

void PortScanManager::startScan(const IPAddress& target, uint16_t portStart, uint16_t portEnd) {
    if (_running) return;
    if (portEnd < portStart) return;

    uint32_t total = (uint32_t)portEnd - portStart + 1;

    if (xSemaphoreTake(_mutex, portMAX_DELAY) == pdTRUE) {
        _openPorts.clear();
        xSemaphoreGive(_mutex);
    }

    _target = target;
    _portStart = portStart;
    _portEnd = portEnd;
    _totalPorts = total;
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
        xTaskCreatePinnedToCore(&PortScanManager::workerTaskEntry, "portscanw", 6144, args, 1, nullptr, 0);
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
        probePort((uint16_t)(_portStart + i));
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
