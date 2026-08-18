#pragma once

#include <IPAddress.h>
#include <atomic>
#include <vector>
#include "../core/EventQueue.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

// SNMP "public" community sweep: for every alive host in ScanManager's
// table it sends one SNMPv2c GET for sysDescr.0 (OID 1.3.6.1.2.1.1.1.0)
// with the community string "public" and, on a reply, records the
// returned system description. An SNMP agent answering "public" is a
// classic, extremely common misconfiguration — the default read
// community exposes model, OS/firmware, interfaces and more to anyone on
// the segment.
//
// Read-only and non-invasive: it only ever issues a GET (never SET), and
// only for the single sysDescr scalar — the SNMP equivalent of a banner
// grab, same risk tier as the port-scan banner grabbing already in this
// firmware. Standard WiFiUDP, no promiscuous mode.
class SnmpSweep {
public:
    struct Responder {
        IPAddress ip;
        String sysDescr;
    };

    void begin(QueueHandle_t outQueue);

    bool start();  // sweeps the current alive-host list; no-op if already running
    bool isRunning() const { return _running; }
    uint8_t progressPct() const { return _progressPct; }

    size_t count() const;
    bool get(size_t index, Responder& out) const;

private:
    static constexpr size_t kMaxResponders = 40;
    static constexpr uint16_t kSnmpPort = 161;
    static constexpr uint16_t kReplyTimeoutMs = 600;

    static void taskEntry(void* arg);
    void run();
    void addResponder(const IPAddress& ip, const String& sysDescr);
    void notify(const String& text);
    void notify(ScanEventType type, uint8_t pct = 0);

    mutable SemaphoreHandle_t _mutex = nullptr;
    std::vector<Responder> _responders;
    QueueHandle_t _outQueue = nullptr;
    std::atomic<bool> _running{false};
    std::atomic<uint8_t> _progressPct{0};
};

extern SnmpSweep g_snmpSweep;
