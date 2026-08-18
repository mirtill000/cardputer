#include "ScanManager.h"
#include "ArpResolver.h"
#include "DeviceClassifier.h"
#include "HostnameResolver.h"
#include "MdnsReverseResolver.h"
#include "OuiDatabase.h"
#include "PingSweep.h"
#include "../core/Config.h"
#include "../net/IpUtil.h"
#include "../net/WifiManager.h"
#include <cstring>

ScanManager g_scanManager;

void ScanManager::begin(QueueHandle_t outQueue) {
    _mutex = xSemaphoreCreateMutex();
    _outQueue = outQueue;
}

void ScanManager::startDiscoveryScan() {
    if (_running) return;
    if (!g_wifi.isConnected()) {
        notify(ScanEventType::LogLine, -1, 0, "wifi not connected");
        return;
    }

    // Custom range overrides the DHCP-derived subnet when set (see
    // setScanRange); otherwise scan the connected /24.
    uint32_t total;
    IPAddress network;
    if (_customCount > 0) {
        total = _customCount;
        network = _customBase;
    } else {
        total = g_wifi.hostCount();
        network = g_wifi.networkAddress();
    }
    if (total == 0) {
        notify(ScanEventType::LogLine, -1, 0, "subnet too small/large to scan");
        return;
    }

    if (xSemaphoreTake(_mutex, portMAX_DELAY) == pdTRUE) {
        _hosts.clear();
        _hosts.reserve(total);
        for (uint32_t i = 1; i <= total; i++) {
            HostInfo h;
            h.ip = iputil::addOffset(network, i);
            _hosts.push_back(h);
        }
        xSemaphoreGive(_mutex);
    }

    _gateway = g_wifi.gatewayIP();
    _totalHosts = total;
    _hostsProbed = 0;
    _progressPct = 0;
    _scanGeneration++;
    _running = true;

    notify(ScanEventType::ScanStarted);

    uint8_t workerCount = g_config.maxConcurrentProbes;
    if (workerCount < 1) workerCount = 1;
    if (workerCount > 8) workerCount = 8;  // hard ceiling: each worker is its own 6KB-stack task
    _workersActive = workerCount;

    for (uint8_t i = 0; i < workerCount; i++) {
        auto* args = new WorkerArgs{this, i, workerCount};
        xTaskCreatePinnedToCore(&ScanManager::workerTaskEntry, "scanw", 6144, args, 1, nullptr, 0);
    }
}

void ScanManager::setScanRange(const IPAddress& base, uint32_t count) {
    if (count > 512) count = 512;  // same ceiling as WifiManager::kMaxScanHosts
    if (count == 0) count = 1;
    _customBase = base;
    _customCount = count;
}

void ScanManager::clearScanRange() { _customCount = 0; }

size_t ScanManager::hostCount() const {
    if (!_mutex || xSemaphoreTake(_mutex, pdMS_TO_TICKS(100)) != pdTRUE) return 0;
    size_t n = _hosts.size();
    xSemaphoreGive(_mutex);
    return n;
}

bool ScanManager::getHost(size_t index, HostInfo& out) const {
    if (!_mutex || xSemaphoreTake(_mutex, pdMS_TO_TICKS(100)) != pdTRUE) return false;
    bool ok = index < _hosts.size();
    if (ok) out = _hosts[index];
    xSemaphoreGive(_mutex);
    return ok;
}

bool ScanManager::getHostByIp(const IPAddress& ip, HostInfo& out) const {
    if (!_mutex || xSemaphoreTake(_mutex, pdMS_TO_TICKS(200)) != pdTRUE) return false;
    bool found = false;
    for (const auto& h : _hosts) {
        if (h.ip == ip) {
            out = h;
            found = true;
            break;
        }
    }
    xSemaphoreGive(_mutex);
    return found;
}

void ScanManager::setHostCredResult(const IPAddress& ip, bool vulnerable, const String& note) {
    if (!_mutex || xSemaphoreTake(_mutex, pdMS_TO_TICKS(300)) != pdTRUE) return;

    for (auto& h : _hosts) {
        if (!(h.ip == ip)) continue;
        h.credAudited = true;
        h.credVulnerable = vulnerable;
        h.credNote = note;
        if (vulnerable) h.risk = RiskLevel::Critical;
        break;
    }

    xSemaphoreGive(_mutex);
}

void ScanManager::setHostPorts(const IPAddress& ip, const std::vector<PortResult>& ports) {
    if (!_mutex || xSemaphoreTake(_mutex, pdMS_TO_TICKS(300)) != pdTRUE) return;

    for (auto& h : _hosts) {
        if (!(h.ip == ip)) continue;

        h.ports = ports;
        bool riskyPortOpen = false;
        String vulnNote;
        for (const auto& p : ports) {
            if (p.port == 21 || p.port == 23 || p.port == 139 || p.port == 445 || p.port == 3389) {
                riskyPortOpen = true;
            }
            if (vulnNote.isEmpty() && p.vulnNote.length()) vulnNote = p.vulnNote;
        }
        // Only ever escalate here, never downgrade: a Critical finding
        // from the credential audit (phase 4) must not be silently
        // reset back to Warning by a later re-scan of the ports.
        if (riskyPortOpen && h.risk == RiskLevel::Ok) h.risk = RiskLevel::Warning;
        // A known-vulnerable banner (VulnSignatures) is a much stronger
        // signal than "a legacy port is merely open" - it's the same
        // strength of finding as a confirmed default credential, so it
        // gets the same Critical treatment.
        if (vulnNote.length()) {
            h.risk = RiskLevel::Critical;
            h.vulnNote = vulnNote;
        }
        break;
    }

    xSemaphoreGive(_mutex);
}

void ScanManager::mergeMdnsService(const IPAddress& ip, const String& type, const String& instance, uint16_t port) {
    if (!_mutex || xSemaphoreTake(_mutex, pdMS_TO_TICKS(300)) != pdTRUE) return;

    static constexpr size_t kMaxServicesPerHost = 8;  // display summary, not the full raw list - see Types.h

    for (auto& h : _hosts) {
        if (!(h.ip == ip)) continue;

        String label = type;
        if (port) {
            label += ':';
            label += String(port);
        }
        if (instance.length()) {
            label += " (";
            label += instance;
            label += ')';
        }

        bool dup = false;
        for (const auto& existing : h.mdnsServices) {
            if (existing == label) {
                dup = true;
                break;
            }
        }
        if (!dup && h.mdnsServices.size() < kMaxServicesPerHost) h.mdnsServices.push_back(label);

        // Adopt the instance name as this host's hostname only if it
        // doesn't have one yet - never overwrites a name NBNS/mDNS
        // reverse-PTR already found during discovery (see probeHost()).
        if (h.hostname.isEmpty() && instance.length()) h.hostname = instance;

        break;
    }

    xSemaphoreGive(_mutex);
}

void ScanManager::workerTaskEntry(void* arg) {
    auto* args = static_cast<WorkerArgs*>(arg);
    ScanManager* self = args->self;
    uint8_t idx = args->workerIndex;
    uint8_t count = args->workerCount;
    delete args;

    self->runWorker(idx, count);
    vTaskDelete(nullptr);
}

void ScanManager::runWorker(uint8_t workerIndex, uint8_t workerCount) {
    uint32_t myGeneration = _scanGeneration;
    size_t total = hostCount();

    // Interleaved partitioning (worker i takes indices i, i+N, i+2N...)
    // rather than contiguous blocks: with N workers running concurrently,
    // this keeps progress reporting even across the whole IP range
    // instead of "finishing" the low end of the subnet first.
    for (size_t idx = workerIndex; idx < total; idx += workerCount) {
        if (_scanGeneration != myGeneration) break;  // a newer scan superseded this one
        probeHost(idx);
        vTaskDelay(pdMS_TO_TICKS(g_config.interProbeDelayMs));
    }

    onWorkerFinished();
}

void ScanManager::probeHost(size_t index) {
    IPAddress ip;
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
        if (index < _hosts.size()) ip = _hosts[index].ip;
        xSemaphoreGive(_mutex);
    }

    bool alive = PingSweep::probe(ip, g_config.scanTimeoutMs);

    uint8_t mac[6];
    bool macFound = ArpResolver::lookupMac(ip, mac);
    if (macFound) alive = true;  // L2 presence counts even if L3 probes were filtered

    bool isGateway = (ip == _gateway);

    String vendor;
    bool vendorFound = macFound && g_ouiDb.lookup(mac, vendor);

    // Opportunistic, best-effort: only worth the extra wait for hosts we
    // already know are up. Try NBNS first (Windows/Samba boxes), then
    // fall back to mDNS reverse PTR (phones/Macs/Chromecasts/most IoT)
    // only if NBNS came up empty - covers a much wider slice of a
    // typical LAN than either alone, at the cost of up to ~350ms extra
    // per host that answers neither (the worst case, already rare: most
    // hosts either answer quickly or this was never going to find a
    // name for them anyway).
    String hostname;
    if (alive) {
        hostname = HostnameResolver::resolve(ip, 200);
        if (hostname.isEmpty()) hostname = MdnsReverseResolver::resolve(ip, 150);
    }

    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
        if (index < _hosts.size()) {
            HostInfo& h = _hosts[index];
            h.alive = alive;
            h.lastSeenMs = millis();
            if (macFound) {
                memcpy(h.mac, mac, 6);
                h.macKnown = true;
            }
            if (vendorFound) h.vendor = vendor;
            if (hostname.length()) h.hostname = hostname;
            DeviceClassifier::classify(h, isGateway);
        }
        xSemaphoreGive(_mutex);
    }

    uint32_t probed = ++_hostsProbed;
    uint32_t total = _totalHosts;
    uint8_t pct = total ? (uint8_t)((probed * 100) / total) : 100;
    _progressPct = pct;

    notify(alive ? ScanEventType::HostChanged : ScanEventType::ScanProgress, (int16_t)index, pct);
}

void ScanManager::onWorkerFinished() {
    if (--_workersActive == 0) {
        _running = false;
        _progressPct = 100;
        notify(ScanEventType::ScanFinished, -1, 100);
    }
}

void ScanManager::notify(ScanEventType type, int16_t hostIndex, uint8_t pct, const char* text) {
    if (!_outQueue) return;
    ScanNotification n;
    n.source = ScanSource::Discovery;
    n.type = type;
    n.hostIndex = hostIndex;
    n.progressPct = pct;
    if (text) n.setText(text);
    xQueueSend(_outQueue, &n, 0);  // non-blocking: a slow/behind UI just misses a tick, not a crash
}
