#pragma once

#include <IPAddress.h>
#include <atomic>
#include "../core/EventQueue.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

// Runs the default-credentials dictionary (DefaultCredsDictionary) against
// one host's already-discovered open ports, in a single background task
// — the dictionary is small (8 entries) and the checkable ports few, so
// the worker-pool pattern ScanManager/PortScanManager use would be
// pointless overhead here.
//
// IMPORTANT — opt-in gate lives in the UI layer, not here: this class
// does not check AppConfig::credAuditEnabled itself. It's a dumb
// mechanism on purpose; the policy decision (has the user seen and
// accepted the disclaimer this session?) belongs entirely to
// CredDisclaimerScreen/CredAuditScreen, which are the only things
// allowed to call startAudit(). Keeping that check in one place, in the
// UI, means there's exactly one gate to audit for correctness rather
// than one per call site.
class CredAuditManager {
public:
    void begin(QueueHandle_t outQueue);

    void startAudit(const IPAddress& target);
    bool isRunning() const { return _running; }
    IPAddress target() const { return _target; }

private:
    static void taskEntry(void* arg);
    void run();
    void notify(ScanEventType type, uint8_t pct = 0);

    bool tryHttpBasicAuth(const IPAddress& ip, uint16_t port, String& userOut, String& passOut);
    bool tryTelnetLogin(const IPAddress& ip, String& userOut, String& passOut);

    QueueHandle_t _outQueue = nullptr;
    IPAddress _target;
    std::atomic<bool> _running{false};
};

extern CredAuditManager g_credAuditManager;
