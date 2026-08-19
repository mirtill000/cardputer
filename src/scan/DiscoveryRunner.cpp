#include "DiscoveryRunner.h"
#include "SsdpDiscovery.h"
#include "ServiceEnumerator.h"
#include "SnmpSweep.h"
#include "DataStoreProbe.h"
#include "IotOtProbe.h"
#include "LdapProbe.h"
#include "NtlmHttpProbe.h"
#include "CdpLldpSniffer.h"
#include "PassiveHostDiscovery.h"
#include "RogueDhcpDetector.h"
#include "BeaconProbeSniffer.h"
#include "ScanManager.h"
#include "../net/WifiManager.h"

DiscoveryRunner g_discoveryRunner;

void DiscoveryRunner::begin(QueueHandle_t outQueue) {
    _outQueue = outQueue;
}

bool DiscoveryRunner::start() {
    if (_running) {
        notify("discovery already running");
        return false;
    }
    _phase = Phase::Idle;
    _progressPct = 0;
    _running = true;
    notify(ScanEventType::ScanStarted);
    if (xTaskCreatePinnedToCore(&DiscoveryRunner::taskEntry, "discall", 6144, this, 1, nullptr, 0) != pdPASS) {
        // Task never started (out of memory) - clear the running flag so
        // the UI doesn't sit on a run with nothing driving it.
        _running = false;
        notify("start failed - out of memory");
        notify(ScanEventType::ScanFinished, 100);
        return false;
    }
    return true;
}

void DiscoveryRunner::stop() { _running = false; }

void DiscoveryRunner::taskEntry(void* arg) {
    static_cast<DiscoveryRunner*>(arg)->run();
    vTaskDelete(nullptr);
}

void DiscoveryRunner::waitOneShot(bool (*isRunning)()) {
    vTaskDelay(pdMS_TO_TICKS(400));  // let the manager flip its running flag
    uint32_t start = millis();
    while (_running && isRunning() && (millis() - start) < kOneShotMaxMs) {
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

void DiscoveryRunner::sleepWindow(uint32_t ms) {
    uint32_t start = millis();
    while (_running && (millis() - start) < ms) vTaskDelay(pdMS_TO_TICKS(250));
}

void DiscoveryRunner::run() {
    if (!g_wifi.isConnected()) {
        setPhase(Phase::Failed, "no WiFi - connect first", 100);
        _running = false;
        notify(ScanEventType::ScanFinished, 100);
        return;
    }

    // Defensive: make sure no promiscuous listener is left running from a
    // previous manual session, so the sequencing below owns the radio.
    g_cdpLldpSniffer.stop();
    g_passiveHostDiscovery.stop();
    g_rogueDhcpDetector.stop();
    g_beaconProbeSniffer.stop();
    vTaskDelay(pdMS_TO_TICKS(300));

    // --- One-shot UDP/TCP queries, sequentially ---
    if (_running) {
        setPhase(Phase::Upnp, "UPnP/SSDP discovery...", 5);
        g_ssdpDiscovery.start();
        waitOneShot([]() { return g_ssdpDiscovery.isRunning(); });
    }
    if (_running) {
        setPhase(Phase::Services, "mDNS service scan...", 15);
        g_serviceEnumerator.start();
        waitOneShot([]() { return g_serviceEnumerator.isRunning(); });

        // Correlate whatever came back to the discovery host table - see
        // ScanManager::mergeMdnsService. Best-effort/no-op per entry for
        // anything whose fromIp isn't a known host (e.g. discovery hasn't
        // run yet, or the reply came from outside the swept range).
        ServiceEnumerator::Service svc;
        for (size_t i = 0; i < g_serviceEnumerator.count(); i++) {
            if (g_serviceEnumerator.get(i, svc)) {
                g_scanManager.mergeMdnsService(svc.fromIp, svc.type, svc.instance, svc.port);
            }
        }
    }
    if (_running) {
        setPhase(Phase::Snmp, "SNMP public sweep...", 25);
        g_snmpSweep.start();
        waitOneShot([]() { return g_snmpSweep.isRunning(); });
    }
    if (_running) {
        setPhase(Phase::DataStore, "data-store sweep...", 35);
        g_dataStoreProbe.start();
        waitOneShot([]() { return g_dataStoreProbe.isRunning(); });
    }
    if (_running) {
        setPhase(Phase::IotOt, "IoT/OT sweep (MQTT/Modbus/CoAP)...", 40);
        g_iotOtProbe.start();
        waitOneShot([]() { return g_iotOtProbe.isRunning(); });
    }
    if (_running) {
        setPhase(Phase::Ldap, "LDAP anon-bind/rootDSE sweep...", 45);
        g_ldapProbe.start();
        waitOneShot([]() { return g_ldapProbe.isRunning(); });
    }
    if (_running) {
        // Needs per-host PORT SCAN results (HostInfo::ports), which a
        // NETWORK SCAN alone doesn't produce - see NtlmHttpProbe.h. Not
        // skipped here: if any host already has a known HTTP port from
        // an earlier session action (a manual port scan, AUTO ASSESS),
        // this still checks it; otherwise it just reports none and
        // moves on, same graceful "nothing to do" as every other phase.
        setPhase(Phase::NtlmHttp, "NTLM-over-HTTP disclosure...", 55);
        g_ntlmHttpProbe.start();
        waitOneShot([]() { return g_ntlmHttpProbe.isRunning(); });
    }

    // --- Promiscuous listeners, one at a time (shared radio callback) ---
    if (_running) {
        setPhase(Phase::LanTopology, "LAN topology (CDP/LLDP)...", 68);
        g_cdpLldpSniffer.start();
        sleepWindow(kPromiscWindowMs);
        g_cdpLldpSniffer.stop();
    }
    if (_running) {
        setPhase(Phase::PassiveHosts, "passive host discovery...", 80);
        g_passiveHostDiscovery.start();
        sleepWindow(kPromiscWindowMs);
        g_passiveHostDiscovery.stop();
    }
    if (_running) {
        setPhase(Phase::RogueDhcp, "rogue DHCP watch...", 92);
        g_rogueDhcpDetector.start();
        sleepWindow(kPromiscWindowMs);
        g_rogueDhcpDetector.stop();
    }
    if (_running) {
        // Last on purpose: unlike the three phases above, this one hops
        // channels and so drops this device's own WiFi connection for its
        // duration (see BeaconProbeSniffer.h) - nothing later in this run
        // needs that connection, and the sniffer reconnects on its own
        // stop().
        setPhase(Phase::BeaconProbe, "beacon/probe survey...", 97);
        g_beaconProbeSniffer.start();
        sleepWindow(kPromiscWindowMs);
        g_beaconProbeSniffer.stop();
    }

    if (_running) {
        setPhase(Phase::Done, "all discovery complete", 100);
    } else {
        setPhase(Phase::Failed, "cancelled", 100);
    }
    _running = false;
    notify(ScanEventType::ScanFinished, 100);
}

void DiscoveryRunner::setPhase(Phase p, const String& msg, uint8_t pct) {
    _phase = p;
    _progressPct = pct;
    notify(msg);
    notify(ScanEventType::ScanProgress, pct);
}

void DiscoveryRunner::notify(const String& text) {
    if (!_outQueue) return;
    ScanNotification n;
    n.source = ScanSource::DiscoveryAll;
    n.type = ScanEventType::LogLine;
    n.setText(text.c_str());
    xQueueSend(_outQueue, &n, 0);
}

void DiscoveryRunner::notify(ScanEventType type, uint8_t pct) {
    if (!_outQueue) return;
    ScanNotification n;
    n.source = ScanSource::DiscoveryAll;
    n.type = type;
    n.progressPct = pct;
    xQueueSend(_outQueue, &n, 0);
}
