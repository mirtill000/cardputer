#pragma once

#include <IPAddress.h>
#include <atomic>
#include <vector>
#include "../core/EventQueue.h"
#include "../core/Types.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

// Owns the discovered-host table and runs the ARP/ping discovery sweep
// across a pool of background FreeRTOS worker tasks.
//
// UI code never touches the table directly: it calls hostCount()/
// getHost(), which take a short mutex lock and copy the requested
// HostInfo out via a normal C++ assignment (String/std::vector deep-copy
// themselves correctly there — this is unrelated to, and doesn't share
// the danger of, the raw-memcpy FreeRTOS-queue case documented in
// core/EventQueue.h).
class ScanManager {
public:
    // outQueue: where ScanNotification events are posted (typically
    // UiManager's scan queue). Must outlive the ScanManager.
    void begin(QueueHandle_t outQueue);

    // Starts a fresh sweep of the currently-connected WiFi subnet.
    // No-op if a scan is already running, or if WiFi isn't connected.
    void startDiscoveryScan();

    // Optional custom target range: overrides the DHCP-derived subnet with
    // an explicit base network address + host count (base+1 .. base+count).
    // Cleared with clearScanRange() to return to the connected /24. Note:
    // ARP/MAC resolution only works for the local subnet, so a range on a
    // different subnet relies on L3 ping via the gateway and won't get MACs.
    void setScanRange(const IPAddress& base, uint32_t count);
    void clearScanRange();
    bool hasCustomRange() const { return _customCount > 0; }
    IPAddress customBase() const { return _customBase; }
    uint32_t customCount() const { return _customCount; }

    bool isRunning() const { return _running; }
    uint8_t progressPct() const { return _progressPct; }

    size_t hostCount() const;
    bool getHost(size_t index, HostInfo& out) const;

    // Same as getHost(), but matched by IP instead of table index —
    // for callers (PortScanManager, CredAuditManager) that only know
    // which host they're working on by its address.
    bool getHostByIp(const IPAddress& ip, HostInfo& out) const;

    // Called by PortScanManager once a per-host port scan finishes, to
    // write the results back into the matching HostInfo row (matched by
    // IP — port scans aren't indexed by the discovery table's row
    // index) and bump risk if a legacy/management port known to bite
    // people (FTP/Telnet/SMB/RDP) is open.
    void setHostPorts(const IPAddress& ip, const std::vector<PortResult>& ports);

    // Called by CredAuditManager once a default-credentials check
    // finishes. A confirmed hit always escalates risk to Critical —
    // this is the one finding in the whole app strong enough to
    // warrant that color, see README.
    void setHostCredResult(const IPAddress& ip, bool vulnerable, const String& note);

    // Called once per discovered mDNS/DNS-SD service instance (see
    // ServiceEnumerator::Service::fromIp) after a browse finishes, by
    // whichever screen/runner drove it (ServiceScreen, DiscoveryRunner) —
    // ScanManager itself never talks to ServiceEnumerator directly, same
    // "narrow IP-matched setter" shape as setHostPorts/setHostCredResult
    // above. No-op if fromIp doesn't match any host already in this
    // table (e.g. the reply came from something outside the discovery
    // sweep's range, or discovery hasn't been run yet this session).
    // Appends a short display line to HostInfo::mdnsServices (deduped,
    // capped) and, if the host has no hostname yet, adopts the mDNS
    // instance name as one — a device's own DNS-SD instance name is
    // often its human-assigned name ("Living Room speaker"), a better
    // source than the generic reverse-PTR lookup already tried during
    // discovery (see ScanManager::probeHost), which plenty of devices
    // simply don't answer.
    void mergeMdnsService(const IPAddress& ip, const String& type, const String& instance, uint16_t port);

private:
    struct WorkerArgs {
        ScanManager* self;
        uint8_t workerIndex;
        uint8_t workerCount;
    };

    static void workerTaskEntry(void* arg);
    void runWorker(uint8_t workerIndex, uint8_t workerCount);
    void probeHost(size_t index);
    void onWorkerFinished();
    void notify(ScanEventType type, int16_t hostIndex = -1, uint8_t pct = 0, const char* text = nullptr);

    mutable SemaphoreHandle_t _mutex = nullptr;
    std::vector<HostInfo> _hosts;
    QueueHandle_t _outQueue = nullptr;

    std::atomic<bool> _running{false};
    std::atomic<uint8_t> _progressPct{0};
    std::atomic<uint8_t> _workersActive{0};
    std::atomic<uint32_t> _hostsProbed{0};
    std::atomic<uint32_t> _totalHosts{0};
    std::atomic<uint32_t> _scanGeneration{0};  // bumped per scan; lets stale workers from a previous run notice and bail

    IPAddress _gateway;

    // Custom range override (see setScanRange). _customCount == 0 means
    // "use the DHCP-derived subnet".
    IPAddress _customBase;
    std::atomic<uint32_t> _customCount{0};
};

extern ScanManager g_scanManager;
