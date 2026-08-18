#pragma once

#include <IPAddress.h>
#include <atomic>
#include <vector>
#include "../core/EventQueue.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

// NTLM-over-HTTP information disclosure: for every alive host with an
// HTTP port already found by NETWORK SCAN/PORT SCAN, sends an NTLM Type 1
// NEGOTIATE_MESSAGE in an "Authorization: NTLM <...>" header and, if the
// server replies with a Type 2 CHALLENGE_MESSAGE (a normal, un-
// authenticated part of the NTLM handshake — see net/NtlmWire.h),
// decodes its TargetInfo to reveal the server's NetBIOS/DNS domain and
// computer name — often the internal AD domain and real hostname behind
// a web app that otherwise gives nothing away.
//
// NEVER completes the handshake and NEVER sends a credential: this stops
// after reading the Type 2 challenge, which is exactly as far as the
// protocol goes before a client would need a real username/password to
// continue. No hash is captured, nothing is relayed anywhere — pure
// information disclosure, the same category as a banner grab. Read-only,
// not behind the credential-attack gate for that reason (same tier as
// LdapProbe/DataStoreProbe/SnmpSweep).
//
// HTTP only (service == "http" from the port scan) - HTTPS endpoints
// (service == "https") are out of scope here: NTLM-over-HTTPS is common
// too (OWA/Exchange, internal IIS sites), but reaching it needs a TLS
// client, a meaningfully bigger addition than this pass covers.
class NtlmHttpProbe {
public:
    struct Finding {
        IPAddress ip;
        uint16_t port = 0;
        // TargetInfo fields from the server's Type 2 challenge - "" if
        // that particular AV_PAIR wasn't sent (see net/NtlmWire.h).
        String netbiosDomain;
        String netbiosComputer;
        String dnsDomain;
        String dnsComputer;
    };

    void begin(QueueHandle_t outQueue);

    bool start();  // sweeps hosts with a known HTTP port; no-op if already running
    bool isRunning() const { return _running; }
    uint8_t progressPct() const { return _progressPct; }

    size_t count() const;
    bool get(size_t index, Finding& out) const;

private:
    static constexpr size_t kMaxFindings = 40;
    static constexpr uint16_t kConnectTimeoutMs = 800;
    static constexpr uint16_t kReadTimeoutMs = 800;

    static void taskEntry(void* arg);
    void run();
    void probeHost(const IPAddress& ip, uint16_t port);
    void addFinding(const Finding& f);
    void notify(const String& text);
    void notify(ScanEventType type, uint8_t pct = 0);

    mutable SemaphoreHandle_t _mutex = nullptr;
    std::vector<Finding> _findings;
    QueueHandle_t _outQueue = nullptr;
    std::atomic<bool> _running{false};
    std::atomic<uint8_t> _progressPct{0};
};

extern NtlmHttpProbe g_ntlmHttpProbe;
