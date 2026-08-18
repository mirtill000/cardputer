#pragma once

#include <IPAddress.h>
#include <atomic>
#include <vector>
#include "../core/EventQueue.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

// Per-host "service audit": given a target already discovered by
// NETWORK SCAN, it looks at that host's open ports and runs the relevant
// authentication/anonymous-access checks against each service, collecting
// findings (open-with-no-auth, default credentials accepted, anonymous
// access allowed).
//
// This is a real credential-attack tool and lives behind the same opt-in
// gate as CredAuditManager (see ServiceAuditScreen — it will not run
// until AppConfig::credAuditEnabled has been set via the disclaimer).
//
// Service coverage and deliberate scope (see README "Limiti noti"):
//   - FTP (21): anonymous login + writable test.
//   - SMB (445): SMB1 negotiate + null SESSION_SETUP (non-extended path);
//     full NetShareEnum/DCE-RPC enumeration is out of scope.
//   - Redis (6379): no-auth check + AUTH default-password attempts.
//   - MySQL (3306): mysql_native_password default creds (uses mbedtls
//     SHA1 — library crypto, not hand-rolled). MySQL 8 caching_sha2 and
//     MSSQL are out of scope.
//   - PostgreSQL (5432): trust / cleartext / MD5 default creds (mbedtls
//     MD5). SCRAM is detected and reported, not brute-forced.
//   - VNC (5900): no-auth check only (a server offering security type
//     "None"). The DES challenge brute was dropped — ESP-IDF's mbedtls
//     ships MBEDTLS_DES_C disabled, so mbedtls_des_* won't link, and
//     hand-rolling DES is out per the no-artisanal-crypto rule.
//   - HTTP (80/8080/8000/8888): Basic-auth default creds. Form-based
//     login brute is out of scope in this pass.
// Each brute uses a small built-in default-credential set (not the full
// wordlist) to keep the binary-protocol handshakes bounded.
class ServiceAuditManager {
public:
    struct Finding {
        String service;
        String result;
        bool critical = false;
    };

    void begin(QueueHandle_t outQueue);

    bool start(const IPAddress& target);  // no-op if already running
    bool isRunning() const { return _running; }
    IPAddress target() const { return _target; }

    size_t count() const;
    bool get(size_t index, Finding& out) const;

private:
    static void taskEntry(void* arg);
    void run();

    void auditFtp(uint16_t port);
    void auditSmb(uint16_t port);
    void auditRedis(uint16_t port);
    void auditMysql(uint16_t port);
    void auditPostgres(uint16_t port);
    void auditVnc(uint16_t port);
    void auditHttp(uint16_t port);

    void addFinding(const char* service, const String& result, bool critical);
    void notify(const String& text);
    void notify(ScanEventType type, uint8_t pct = 0);

    mutable SemaphoreHandle_t _mutex = nullptr;
    std::vector<Finding> _findings;
    QueueHandle_t _outQueue = nullptr;
    std::atomic<bool> _running{false};
    IPAddress _target;
};

extern ServiceAuditManager g_serviceAuditManager;
