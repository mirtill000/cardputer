#include "AssessmentRunner.h"
#include "../core/ScanLog.h"
#include "../core/RunClaim.h"
#include "ScanManager.h"
#include "PortScanManager.h"
#include "../core/Types.h"
#include "../net/WifiManager.h"
#include "../storage/ReportGenerator.h"
#include "../storage/SdCard.h"
#include "../storage/NetrunnerPaths.h"
#include <vector>

AssessmentRunner g_assessmentRunner;

void AssessmentRunner::begin(QueueHandle_t outQueue) {
    _mutex = xSemaphoreCreateMutex();
    _outQueue = outQueue;
}

bool AssessmentRunner::start() {
    RunClaim claim(_running);
    if (!claim.owned()) {
        notify("assessment already running");
        return false;
    }
    _phase = Phase::Idle;
    _progressPct = 0;
    _hostsTotal = 0;
    _hostsDone = 0;
    _reportOk = false;
    claim.commit();
    notify(ScanEventType::ScanStarted);
    if (xTaskCreatePinnedToCore(&AssessmentRunner::taskEntry, "assess", 6144, this, 1, nullptr, 0) != pdPASS) {
        _running = false;
        setPhase(Phase::Failed, "failed to start assessment task");
        notify(ScanEventType::ScanFinished, 100);
        return false;
    }
    return true;
}

void AssessmentRunner::stop() { _running = false; }

void AssessmentRunner::taskEntry(void* arg) {
    static_cast<AssessmentRunner*>(arg)->run();
    vTaskDelete(nullptr);
}

void AssessmentRunner::run() {
    if (!g_wifi.isConnected()) {
        setPhase(Phase::Failed, "no WiFi - connect first");
        _running = false;
        notify(ScanEventType::ScanFinished, 100);
        return;
    }

    // --- Phase 1: discovery (0-40% of the overall bar) ---
    setPhase(Phase::Discovery, "discovery scan...");
    // Must be OUR scan: if SENTINEL/PLAYBOOK/the UI already has one in
    // flight, wait for it and start a fresh one rather than reading a
    // host table this assessment didn't produce. _running is set
    // synchronously by a successful start, so no settle delay is needed.
    StartResult started = g_scanManager.startDiscoveryScanWhenIdle(kScanBusyWaitMs);
    if (started != StartResult::Started) {
        setPhase(Phase::Failed, String("discovery scan not started: ") + startResultLabel(started));
        _running = false;
        notify(ScanEventType::ScanFinished, 100);
        return;
    }
    while (_running && g_scanManager.isRunning()) {
        _progressPct = (uint8_t)(g_scanManager.progressPct() * 40 / 100);
        notify(ScanEventType::ScanProgress, _progressPct);
        vTaskDelay(pdMS_TO_TICKS(250));
    }
    if (!_running) {
        setPhase(Phase::Failed, "cancelled");
        notify(ScanEventType::ScanFinished, 100);
        return;
    }

    // Snapshot alive hosts.
    std::vector<IPAddress> targets = g_scanManager.aliveHostIps();
    _hostsTotal = (uint16_t)targets.size();

    // --- Phase 2: port scan each host in turn (40-90%) ---
    setPhase(Phase::PortScan, String("port-scanning ") + String((unsigned)targets.size()) + " host(s)");
    for (size_t i = 0; i < targets.size() && _running; i++) {
        StartResult ps = g_portScanManager.startScan(targets[i], kPortStart, kPortEnd);
        if (ps == StartResult::Busy) {
            // Someone else's port scan (e.g. started from HOST DETAIL):
            // let it finish, then run ours - never wait on and count
            // a scan of a different host as this one's.
            uint32_t waitStart = millis();
            while (_running && g_portScanManager.isRunning() && (millis() - waitStart) < kScanBusyWaitMs) {
                vTaskDelay(pdMS_TO_TICKS(250));
            }
            ps = g_portScanManager.startScan(targets[i], kPortStart, kPortEnd);
        }
        if (ps == StartResult::Started) {
            while (_running && g_portScanManager.isRunning()) vTaskDelay(pdMS_TO_TICKS(250));
        } else {
            notify(String("skipped ") + targets[i].toString() + ": port scan " + startResultLabel(ps));
        }
        _hostsDone = (uint16_t)(i + 1);
        _progressPct = (uint8_t)(40 + ((i + 1) * 50) / (targets.empty() ? 1 : targets.size()));
        notify(ScanEventType::ScanProgress, _progressPct);
    }
    if (!_running) {
        setPhase(Phase::Failed, "cancelled");
        notify(ScanEventType::ScanFinished, 100);
        return;
    }

    // --- Phase 3: report (90-100%) ---
    setPhase(Phase::Report, "generating report...");
    fs::FS& fs = sdcard::exportFs();
    String path = netrunner::reportBase(fs, g_wifi.currentSsid()) + ".html";
    bool ok = ReportGenerator::generate(fs, path.c_str());
    _reportOk = ok;
    if (_mutex && xSemaphoreTake(_mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
        _reportPath = ok ? (String("(") + sdcard::exportFsLabel() + ") " + path) : String("report FAILED");
        xSemaphoreGive(_mutex);
    }
    _progressPct = 100;

    setPhase(Phase::Done, ok ? "assessment complete" : "done (report failed)");
    _running = false;
    notify(ScanEventType::ScanFinished, 100);
}

void AssessmentRunner::setPhase(Phase p, const String& msg) {
    _phase = p;
    notify(msg);
}

void AssessmentRunner::notify(const String& text) { scanlog::post(_outQueue, ScanSource::Assessment, text); }

void AssessmentRunner::notify(ScanEventType type, uint8_t pct) {
    if (!_outQueue) return;
    ScanNotification n;
    n.source = ScanSource::Assessment;
    n.type = type;
    n.progressPct = pct;
    xQueueSend(_outQueue, &n, 0);
}

String AssessmentRunner::reportPath() const {
    String r;
    if (_mutex && xSemaphoreTake(_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        r = _reportPath;
        xSemaphoreGive(_mutex);
    }
    return r;
}
