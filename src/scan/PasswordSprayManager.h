#pragma once

#include <Arduino.h>
#include <IPAddress.h>
#include <atomic>
#include <vector>
#include "../core/EventQueue.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

// Password spray: one shared password tried across MANY hosts/services/
// users, rate-limited, deliberately shaped to STAY UNDER account-lockout
// thresholds — the exact inversion of the brute-force pattern
// CredAuditManager uses (many passwords against one user/host, which
// triggers lockout on the first ~5 failures on any hardened system).
//
// The spray tries the single password across:
//   * every discovered host with an open HTTP/Telnet/FTP/POP3/IMAP/SMTP
//     port (host list drawn from ScanManager, filtered by kMaxHosts),
//   * against a small username list (built-in kSprayUsers + optional
//     /creds/users.txt from SD, WordlistLoader),
// with kInterAttemptDelayMs of gap between attempts and a "one login
// per user per host per hour"-style cadence (one full pass, then stop —
// no re-attempts, that's what would trip lockout).
//
// OFFENSIVE. Reuses CredAuditManager::tryLogin() for the actual protocol
// handshakes so all six services stay in one tested place. Gated exactly
// like the credential audit (AppConfig::credAuditEnabled).
class PasswordSprayManager {
public:
    struct Hit {
        IPAddress ip;
        String service;   // "http" / "telnet" / "ftp" / "pop3" / "imap" / "smtp"
        uint16_t port = 0;
        String user;
    };

    void begin(QueueHandle_t outQueue);

    bool start(const String& password);  // no-op if already running / empty pw
    void stop();
    bool isRunning() const { return _running; }
    String password() const { return _password; }

    uint32_t attempts() const { return _attempts; }
    uint32_t targets() const { return _targets; }
    size_t count() const;
    bool get(size_t index, Hit& out) const;

private:
    static constexpr size_t kMaxHosts = 32;
    static constexpr size_t kMaxHits = 32;
    static constexpr uint16_t kInterAttemptDelayMs = 300;  // above interProbeDelayMs — spray is deliberately gentle

    static void taskEntry(void* arg);
    void run();
    void tryOnHost(const IPAddress& ip, const char* service, uint16_t port,
                   const std::vector<String>& users);
    void addHit(const Hit& h);
    void notify(const String& text);
    void notify(ScanEventType type, uint8_t pct = 0);

    QueueHandle_t _outQueue = nullptr;
    std::atomic<bool> _running{false};
    std::atomic<uint32_t> _attempts{0};
    std::atomic<uint32_t> _targets{0};

    String _password;

    mutable SemaphoreHandle_t _mutex = nullptr;
    std::vector<Hit> _hits;
};

extern PasswordSprayManager g_passwordSpray;
