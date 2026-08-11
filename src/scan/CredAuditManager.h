#pragma once

#include <IPAddress.h>
#include <atomic>
#include <vector>
#include "../core/EventQueue.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

// Real credential-attack tool: tries the built-in quick dictionary
// (DefaultCredsDictionary, 8 well-known pairs) first, then every
// username x password combination from the wordlists in
// data/creds/{users,passwords}.txt against a host's already-discovered
// HTTP, Telnet or FTP login — one attempt at a time, rate-limited via
// AppConfig::interProbeDelayMs. This is NOT limited to "known defaults"
// anymore; see CredDisclaimerScreen for the disclaimer text that
// reflects that.
//
// Single background task, not a worker pool (unlike ScanManager/
// PortScanManager): a real attack against one login should never be
// parallelized — it would just make many simultaneous auth failures
// against the same service, which is both easier for the target to
// notice/rate-limit and not meaningfully faster given the rate limit
// applies per attempt regardless.
//
// IMPORTANT — opt-in gate lives in the UI layer, not here: this class
// does not check AppConfig::credAuditEnabled itself. See
// CredDisclaimerScreen/CredAuditScreen, the only things allowed to
// call startAudit().
class CredAuditManager {
public:
    void begin(QueueHandle_t outQueue);

    void startAudit(const IPAddress& target);
    bool isRunning() const { return _running; }
    IPAddress target() const { return _target; }

    // Live progress, valid both while running and after completion.
    uint32_t attemptCount() const { return _attempts; }
    uint32_t successCount() const { return _successes; }

private:
    static void taskEntry(void* arg);
    void run();
    void ensureWordlistsLoaded();
    void notify(ScanEventType type, uint8_t pct = 0);
    void logAttempt(const char* service, const String& user, const String& pass, bool success);

    // Tries every dictionary + wordlist combo against one service;
    // returns true and fills outUser/outPass on the first hit.
    bool attemptService(const char* serviceName, uint16_t port, String& outUser, String& outPass);

    bool tryHttpBasicAuth(const IPAddress& ip, uint16_t port, const String& user, const String& pass);
    bool tryTelnetLogin(const IPAddress& ip, const String& user, const String& pass);
    bool tryFtpLogin(const IPAddress& ip, const String& user, const String& pass);
    // Added alongside the "offensive tools" batch — three more textual,
    // RFC-response-code protocols in the same spirit as tryFtpLogin
    // (deterministic status codes, not heuristic prompt-scraping like
    // Telnet). SSH deliberately NOT added here or anywhere in this
    // class: a real SSH login needs a full client-side handshake (key
    // exchange, host-key handling, symmetric cipher negotiation) — a
    // hand-rolled implementation of that, never tested against real
    // hardware, is a correctness AND security risk this project isn't
    // taking on. If SSH credential guessing is wanted later, the right
    // path is a verified third-party ESP32 SSH client library as a real
    // lib_deps dependency, not primitives built from scratch here.
    bool tryPop3Login(const IPAddress& ip, const String& user, const String& pass);
    bool tryImapLogin(const IPAddress& ip, const String& user, const String& pass);
    bool trySmtpLogin(const IPAddress& ip, const String& user, const String& pass);

    QueueHandle_t _outQueue = nullptr;
    IPAddress _target;
    std::atomic<bool> _running{false};
    std::atomic<uint32_t> _attempts{0};
    std::atomic<uint32_t> _successes{0};

    bool _wordlistsLoaded = false;
    std::vector<String> _users;
    std::vector<String> _passwords;
};

extern CredAuditManager g_credAuditManager;
