#pragma once

#include <IPAddress.h>
#include <atomic>
#include <vector>
#include "../core/EventQueue.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

// Unauthenticated data-store detection: sweeps the alive-host list and
// checks the standard ports of the data stores most commonly left exposed
// with no authentication on a LAN:
//   Redis (6379), Memcached (11211), Elasticsearch (9200), MongoDB (27017).
// For each it sends a benign, read-only command (Redis PING/INFO,
// Memcached version, an Elasticsearch GET /, a MongoDB isMaster +
// listDatabases) and reports whether the store answered and, where it can
// tell, whether it did so WITHOUT requiring authentication — an open data
// store is one of the highest-impact findings on an internal network.
//
// Read-only detection, same risk tier as the SNMP "public" sweep and the
// SMB negotiate check: it never writes, deletes, or authenticates. Not
// behind the credential-attack gate for that reason.
class DataStoreProbe {
public:
    struct Finding {
        IPAddress ip;
        String store;    // "redis" / "memcached" / "elasticsearch" / "mongodb"
        String detail;   // version string or a short note
        bool noAuth = false;  // answered a privileged command without auth
    };

    void begin(QueueHandle_t outQueue);

    bool start();  // sweeps current alive hosts; no-op if already running
    bool isRunning() const { return _running; }
    uint8_t progressPct() const { return _progressPct; }

    size_t count() const;
    bool get(size_t index, Finding& out) const;

private:
    static constexpr size_t kMaxFindings = 40;
    static constexpr uint16_t kConnectTimeoutMs = 700;
    static constexpr uint16_t kReadTimeoutMs = 700;

    static void taskEntry(void* arg);
    void run();
    void probeHost(const IPAddress& ip);
    void addFinding(const IPAddress& ip, const char* store, const String& detail, bool noAuth);
    void notify(const String& text);
    void notify(ScanEventType type, uint8_t pct = 0);

    mutable SemaphoreHandle_t _mutex = nullptr;
    std::vector<Finding> _findings;
    QueueHandle_t _outQueue = nullptr;
    std::atomic<bool> _running{false};
    std::atomic<uint8_t> _progressPct{0};
};

extern DataStoreProbe g_dataStoreProbe;
