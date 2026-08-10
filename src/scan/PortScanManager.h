#pragma once

#include <IPAddress.h>
#include <atomic>
#include <vector>
#include "../core/EventQueue.h"
#include "../core/Types.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

// Scans one host's TCP port range in the background. Same
// worker-pool-of-blocking-sockets design as ScanManager (see its header
// for the rationale), just partitioned across *ports* of one host
// instead of *hosts* of one subnet — and it only keeps OPEN ports (a
// 1-1024 sweep with mostly-closed results would otherwise mean carrying
// ~1000 uninteresting entries around).
//
// A singleton, not one instance per host: only one port scan is ever
// meant to run at a time (the UI only exposes "scan ports of the host
// I'm currently looking at"), and results feed back into ScanManager's
// HostInfo for that IP once done (see ScanManager::setHostPorts) so
// they persist even after this scanner moves on to a different host.
class PortScanManager {
public:
    void begin(QueueHandle_t outQueue);

    // No-op if a scan is already running or the range is invalid.
    void startScan(const IPAddress& target, uint16_t portStart, uint16_t portEnd);

    bool isRunning() const { return _running; }
    uint8_t progressPct() const { return _progressPct; }
    IPAddress target() const { return _target; }
    bool hasScannedAnything() const { return _hasResult; }

    size_t resultCount() const;
    bool getResult(size_t index, PortResult& out) const;

private:
    struct WorkerArgs {
        PortScanManager* self;
        uint8_t workerIndex;
        uint8_t workerCount;
    };

    static void workerTaskEntry(void* arg);
    void runWorker(uint8_t workerIndex, uint8_t workerCount);
    void probePort(uint16_t port);
    void onWorkerFinished();
    void notify(ScanEventType type, int16_t index = -1, uint8_t pct = 0);

    mutable SemaphoreHandle_t _mutex = nullptr;
    std::vector<PortResult> _openPorts;
    QueueHandle_t _outQueue = nullptr;

    IPAddress _target;
    uint16_t _portStart = 1;
    uint16_t _portEnd = 1024;

    std::atomic<bool> _running{false};
    std::atomic<bool> _hasResult{false};  // true once a scan has completed at least once
    std::atomic<uint8_t> _progressPct{0};
    std::atomic<uint8_t> _workersActive{0};
    std::atomic<uint32_t> _portsProbed{0};
    std::atomic<uint32_t> _totalPorts{0};
    std::atomic<uint32_t> _scanGeneration{0};
};

extern PortScanManager g_portScanManager;
