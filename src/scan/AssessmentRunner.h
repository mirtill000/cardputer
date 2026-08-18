#pragma once

#include <Arduino.h>
#include <atomic>
#include "../core/EventQueue.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

// "One-button assessment": chains the safe, non-gated steps of a basic
// LAN sweep into a single run — discovery scan, then a port scan of every
// alive host in turn, then an HTML kill-chain report to SD. It only
// *drives* the existing managers (ScanManager, PortScanManager,
// ReportGenerator) via their public APIs and waits on their isRunning()
// flags; it re-implements none of them.
//
// Credential auditing is deliberately NOT chained in: it sits behind a
// per-session authorization gate (see CredAuditManager / the offensive
// disclaimer) and running it automatically across every host would
// bypass the explicit-consent model the rest of the firmware keeps. The
// workflow stops at "what's here and what's open" plus the report; the
// user still triggers any credential work per host by hand.
class AssessmentRunner {
public:
    enum class Phase : uint8_t { Idle, Discovery, PortScan, Report, Done, Failed };

    void begin(QueueHandle_t outQueue);

    bool start();  // no-op if already running
    void stop();   // request cancellation; the task finishes its current step and bails
    bool isRunning() const { return _running; }

    Phase phase() const { return _phase; }
    uint8_t progressPct() const { return _progressPct; }
    uint16_t hostsTotal() const { return _hostsTotal; }
    uint16_t hostsDone() const { return _hostsDone; }
    bool reportOk() const { return _reportOk; }
    String reportPath() const;

private:
    static constexpr uint16_t kPortStart = 1;
    static constexpr uint16_t kPortEnd = 1024;

    static void taskEntry(void* arg);
    void run();
    void setPhase(Phase p, const String& msg);
    void notify(const String& text);
    void notify(ScanEventType type, uint8_t pct = 0);

    mutable SemaphoreHandle_t _mutex = nullptr;  // guards _reportPath
    QueueHandle_t _outQueue = nullptr;
    std::atomic<bool> _running{false};
    std::atomic<Phase> _phase{Phase::Idle};
    std::atomic<uint8_t> _progressPct{0};
    std::atomic<uint16_t> _hostsTotal{0};
    std::atomic<uint16_t> _hostsDone{0};
    std::atomic<bool> _reportOk{false};
    String _reportPath;
};

extern AssessmentRunner g_assessmentRunner;
