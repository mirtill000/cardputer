#include "AssessmentRunner.h"
#include "ScanManager.h"
#include "PortScanManager.h"
#include "../core/Types.h"
#include "../net/WifiManager.h"
#include "../storage/ReportGenerator.h"
#include "../storage/SdCard.h"
#include <vector>

AssessmentRunner g_assessmentRunner;

void AssessmentRunner::begin(QueueHandle_t outQueue) {
    _mutex = xSemaphoreCreateMutex();
    _outQueue = outQueue;
}

bool AssessmentRunner::start() {
    if (_running) return false;
    _phase = Phase::Idle;
    _progressPct = 0;
    _hostsTotal = 0;
    _hostsDone = 0;
    _reportOk = false;
    _running = true;
    notify(ScanEventType::ScanStarted);
    xTaskCreatePinnedToCore(&AssessmentRunner::taskEntry, "assess", 6144, this, 1, nullptr, 0);
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
    g_scanManager.startDiscoveryScan();
    vTaskDelay(pdMS_TO_TICKS(400));  // let it flip _running before we poll
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
    std::vector<IPAddress> targets;
    size_t n = g_scanManager.hostCount();
    HostInfo h;
    for (size_t i = 0; i < n; i++) {
        if (g_scanManager.getHost(i, h) && h.alive) targets.push_back(h.ip);
    }
    _hostsTotal = (uint16_t)targets.size();

    // --- Phase 2: port scan each host in turn (40-90%) ---
    setPhase(Phase::PortScan, String("port-scanning ") + String((unsigned)targets.size()) + " host(s)");
    for (size_t i = 0; i < targets.size() && _running; i++) {
        g_portScanManager.startScan(targets[i], kPortStart, kPortEnd);
        vTaskDelay(pdMS_TO_TICKS(300));
        while (_running && g_portScanManager.isRunning()) vTaskDelay(pdMS_TO_TICKS(250));
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
    bool ok = ReportGenerator::generate(sdcard::exportFs(), "/report.html");
    _reportOk = ok;
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
        _reportPath = ok ? (String("(") + sdcard::exportFsLabel() + ") /report.html") : String("report FAILED");
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

void AssessmentRunner::notify(const String& text) {
    if (!_outQueue) return;
    ScanNotification n;
    n.source = ScanSource::Assessment;
    n.type = ScanEventType::LogLine;
    n.setText(text.c_str());
    xQueueSend(_outQueue, &n, 0);
}

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
