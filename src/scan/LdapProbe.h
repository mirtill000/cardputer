#pragma once

#include <IPAddress.h>
#include <atomic>
#include <vector>
#include "../core/EventQueue.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

// Unauthenticated LDAP recon: sweeps the alive-host list and, for every
// host answering on 389, checks two things — whether an anonymous
// SIMPLE bind (empty DN, empty password) is accepted, and whether the
// rootDSE (the unauthenticated "about this server" pseudo-entry every
// LDAPv3 server exposes at base="") discloses its naming contexts and
// DNS hostname. Both are checked regardless of each other: a hardened
// Active Directory DC commonly rejects anonymous bind (the modern
// default) yet still answers a rootDSE search — per RFC 4511 §5.1 that's
// meant to work even unauthenticated, so a "bind rejected, rootDSE still
// readable" pair is itself an accurate, useful pair of findings, not a
// contradiction. An accepted anonymous bind is the stronger, higher-
// impact finding (older/misconfigured DCs, most OpenLDAP defaults).
//
// Read-only, same risk tier as the SNMP "public" sweep and the SMB null-
// session/data-store checks already in this firmware: one bind attempt
// with no real credentials and one read-only search against a pseudo-
// entry every LDAP server is designed to expose to anyone. Never writes,
// modifies, or attempts a real (non-empty) credential. Not behind the
// credential-attack gate for that reason.
//
// See net/LdapWire.h for the hand-rolled BER (ASN.1) message building/
// parsing this relies on — verified against a real LDAP ASN.1 library
// before being used here, but never against a live LDAP server or a real
// ESP32 build (see README's testing note).
class LdapProbe {
public:
    struct Finding {
        IPAddress ip;
        bool anonymousBindAllowed = false;
        // rootDSE attributes, first value only if multi-valued - "" if
        // this attribute wasn't disclosed (locked down, or genuinely
        // absent). See net/LdapWire.h's parseSearchResultEntry.
        String namingContexts;
        String defaultNamingContext;
        String dnsHostName;
    };

    void begin(QueueHandle_t outQueue);

    bool start();  // sweeps current alive hosts; no-op if already running
    bool isRunning() const { return _running; }
    uint8_t progressPct() const { return _progressPct; }

    size_t count() const;
    bool get(size_t index, Finding& out) const;

private:
    static constexpr size_t kMaxFindings = 40;
    static constexpr uint16_t kLdapPort = 389;
    static constexpr uint16_t kConnectTimeoutMs = 700;
    static constexpr uint16_t kReadTimeoutMs = 700;

    static void taskEntry(void* arg);
    void run();
    void probeHost(const IPAddress& ip);
    void addFinding(const Finding& f);
    void notify(const String& text);
    void notify(ScanEventType type, uint8_t pct = 0);

    mutable SemaphoreHandle_t _mutex = nullptr;
    std::vector<Finding> _findings;
    QueueHandle_t _outQueue = nullptr;
    std::atomic<bool> _running{false};
    std::atomic<uint8_t> _progressPct{0};
};

extern LdapProbe g_ldapProbe;
