#pragma once

#include <Arduino.h>
#include <IPAddress.h>
#include <atomic>
#include <vector>
#include "../core/EventQueue.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

// IoT default-credential sweep: fingerprints each discovered host (OUI
// vendor + service banners) and, on a match, tries that device's
// documented factory default(s) on its HTTP/Telnet login — plus a few
// generic defaults on everything. This is the "is the camera/router still
// on the password it shipped with" check, not a brute-forcer: it tries a
// short, fixed set of well-known defaults (IotDefaultCreds) and stops on
// the first success per service.
//
// OFFENSIVE — it attempts real logins, so it is gated exactly like the
// credential audit (AppConfig::credAuditEnabled, set via the disclaimer):
// the SCREEN enforces that gate before calling start(); this class does
// not check it itself, same contract as CredAuditManager. Reuses
// CredAuditManager::tryLogin() for the actual protocol work.
//
// Needs a NETWORK SCAN (+ port scan) to have found hosts with HTTP/Telnet.
class IotCredScanner {
public:
    struct Hit {
        IPAddress ip;
        String service;   // "http" / "telnet"
        String user;
        String pass;
        String device;    // matched fingerprint keyword, or "generic"
    };

    void begin(QueueHandle_t outQueue);

    bool start();  // no-op if already running
    void stop();
    bool isRunning() const { return _running; }

    uint32_t attemptCount() const { return _attempts; }

    size_t count() const;
    bool get(size_t index, Hit& out) const;

private:
    static constexpr size_t kMaxHosts = 12;

    static void taskEntry(void* arg);
    void run();
    void sweepHost(const IPAddress& ip, uint16_t httpPort, bool hasTelnet, const String& fingerprint);
    void addHit(const Hit& h);
    void notify(const String& text);
    void notify(ScanEventType type, uint8_t pct = 0);

    QueueHandle_t _outQueue = nullptr;
    std::atomic<bool> _running{false};
    std::atomic<uint32_t> _attempts{0};

    mutable SemaphoreHandle_t _mutex = nullptr;
    std::vector<Hit> _hits;
};

extern IotCredScanner g_iotCredScanner;
