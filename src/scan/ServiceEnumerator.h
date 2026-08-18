#pragma once

#include <Arduino.h>
#include <IPAddress.h>
#include <atomic>
#include <vector>
#include "../core/EventQueue.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

// mDNS/DNS-SD service browser: a richer companion to the reverse-PTR
// hostname lookup in MdnsReverseResolver. It performs the standard
// two-level DNS-SD "meta-query": first PTR of
// `_services._dns-sd._udp.local` to enumerate every service TYPE the
// network advertises (`_airplay._tcp`, `_ipp._tcp`, `_googlecast._tcp`,
// `_smb._tcp`, ...), then a PTR of each type to list the named
// INSTANCES (per RFC 6763). When an SRV record accompanies an instance
// in the same response, its port is captured too.
//
// SCOPE: type + instance name (+ port when volunteered). It does not
// chase SRV targets down to A records for the backing IP — the instance
// name plus the already-discovered host table give more than enough
// context, and each extra query round-trip is time on a shared radio.
// `fromIp` below is NOT that A-record chase — it costs nothing extra: an
// mDNS reply for an instance is, per RFC 6762 convention, sent BY the
// device offering that service, so the UDP packet's own source address
// (already available the moment the reply arrives, no additional query)
// is a good best-effort proxy for "which host this is" — the same
// zero-extra-cost reasoning SsdpDiscovery already uses for its
// Device::fromIp. Best-effort like everything else here: a reply relayed
// or proxied by something other than the actual service host would give
// a misleading fromIp, but that's an unusual setup on a home/office LAN.
//
// Low risk: standard multicast queries over WiFiUDP, no promiscuous
// mode, no raw-frame parsing — reuses the DnsWire helpers (verified
// against a reference, see net/DnsWire.h) and the exact multicast
// receive pattern from MdnsReverseResolver.
class ServiceEnumerator {
public:
    struct Service {
        String type;      // e.g. "_airplay._tcp" (".local" trimmed for display)
        String instance;  // human-readable instance label
        uint16_t port = 0;
        IPAddress fromIp;  // source IP of the reply that announced this instance - see the class comment
    };

    void begin(QueueHandle_t outQueue);

    bool start();  // no-op if already running
    bool isRunning() const { return _running; }

    size_t count() const;
    bool get(size_t index, Service& out) const;  // most-recently-added first

private:
    static constexpr size_t kMaxServices = 40;
    static constexpr size_t kMaxTypes = 16;
    static constexpr uint32_t kTypeWindowMs = 1200;
    static constexpr uint32_t kInstanceWindowMs = 900;

    static void taskEntry(void* arg);
    void run();
    void addService(const Service& s);
    void notify(const String& text);
    void notify(ScanEventType type, uint8_t pct = 0);

    mutable SemaphoreHandle_t _mutex = nullptr;
    std::vector<Service> _services;
    QueueHandle_t _outQueue = nullptr;
    std::atomic<bool> _running{false};
};

extern ServiceEnumerator g_serviceEnumerator;
