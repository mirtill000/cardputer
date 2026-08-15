#include "ActivityStatus.h"
#include "Theme.h"
#include "../scan/ArpSpoofManager.h"
#include "../scan/DeauthManager.h"
#include "../scan/PmkidManager.h"
#include "../scan/CdpLldpSniffer.h"
#include "../scan/RogueDhcpDetector.h"
#include "../scan/PassiveHostDiscovery.h"
#include "../scan/BeaconProbeSniffer.h"
#include "../scan/DeauthWatcher.h"
#include "../scan/SentinelManager.h"
#include "../scan/WardrivingManager.h"
#include "../scan/ScanManager.h"
#include "../scan/PortScanManager.h"
#include "../scan/ServiceEnumerator.h"
#include "../scan/SsdpDiscovery.h"
#include "../scan/SnmpSweep.h"
#include "../scan/DataStoreProbe.h"
#include "../scan/LdapProbe.h"
#include "../scan/NtlmHttpProbe.h"
#include "../scan/ServiceAuditManager.h"
#include "../scan/CredAuditManager.h"
#include "../scan/HttpPathBruteforcer.h"
#include "../scan/SmbNegotiateCheck.h"
#include "../scan/AssessmentRunner.h"
#include "../scan/EvilTwinManager.h"
#include "../scan/DiscoveryRunner.h"
#include "../scan/PmkidSweepManager.h"
#include "../scan/IotOtProbe.h"
#include "../scan/PlaybookRunner.h"
#include <cstdio>
#include <cstring>

namespace {
struct TaskEntry {
    bool on;
    const char* shortTag;  // 3-6 chars, for the compact header widget
    const char* label;     // full name, for the ACTIVITY screen
    bool isRf;              // true = promiscuous-mode radio consumer
};

// Fase 37: single source of truth for every long-running manager this
// firmware tracks, shared by draw() (the compact header tag) and
// activity::list() (the full ACTIVITY screen) so the two can never drift
// out of sync with each other. RF entries (promiscuous radio consumers)
// first, then everything else - same grouping/order this file always
// used before the two were split into separate literal tables.
size_t buildTaskTable(TaskEntry* out, size_t cap) {
    size_t n = 0;
    auto add = [&](bool on, const char* shortTag, const char* label, bool isRf) {
        if (n < cap) out[n++] = {on, shortTag, label, isRf};
    };
    add(g_arpSpoofManager.isRunning(), "ARP", "ARP SPOOF DETECT", true);
    add(g_deauthManager.isRunning(), "DTH", "PMKID/DEAUTH CAPTURE", true);
    add(g_pmkidManager.isRunning(), "PMK", "PMKID CAPTURE", true);
    add(g_cdpLldpSniffer.isRunning(), "CDP", "LAN TOPOLOGY", true);
    add(g_rogueDhcpDetector.isRunning(), "DHCP", "ROGUE DHCP", true);
    add(g_passiveHostDiscovery.isRunning(), "PSV", "PASSIVE HOSTS", true);
    add(g_beaconProbeSniffer.isRunning(), "BCN", "BEACON/PROBE INTEL", true);
    add(g_deauthWatcher.isRunning(), "GRD", "GUARD MODE", true);
    add(g_sentinelManager.isRunning(), "SNT", "SENTINEL MODE", true);

    add(g_discoveryRunner.isRunning(), "ALL", "RUN ALL DISCOVERY", false);
    add(g_wardrivingManager.isRunning(), "WD", "WAR DRIVING", false);
    add(g_scanManager.isRunning(), "SCN", "NETWORK SCAN", false);
    add(g_portScanManager.isRunning(), "PORT", "PORT SCAN", false);
    add(g_serviceEnumerator.isRunning(), "SVC", "SERVICE SCAN", false);
    add(g_ssdpDiscovery.isRunning(), "UPNP", "UPNP DISCOVERY", false);
    add(g_snmpSweep.isRunning(), "SNMP", "SNMP SWEEP", false);
    add(g_dataStoreProbe.isRunning(), "DS", "DATASTORE SWEEP", false);
    add(g_iotOtProbe.isRunning(), "IOT", "IOT/OT SWEEP", false);
    add(g_ldapProbe.isRunning(), "LDAP", "LDAP SWEEP", false);
    add(g_ntlmHttpProbe.isRunning(), "NTLM", "NTLM DISCLOSURE", false);
    add(g_serviceAuditManager.isRunning(), "AUD", "SERVICE AUDIT", false);
    add(g_credAuditManager.isRunning(), "CRED", "CREDENTIAL AUDIT", false);
    add(g_httpBruteforcer.isRunning(), "HTTP", "HTTP PATH BRUTEFORCE", false);
    add(g_smbCheck.isRunning(), "SMB", "SMB NEGOTIATE CHECK", false);
    add(g_assessmentRunner.isRunning(), "ASSESS", "AUTO ASSESS", false);
    add(g_evilTwinManager.isRunning(), "TWIN", "EVIL TWIN", false);
    add(g_pmkidSweepManager.isRunning(), "PSWP", "PMKID SWEEP", false);
    add(g_playbookRunner.isRunning(), "PBK", "PLAYBOOK", false);
    return n;
}
}  // namespace

size_t activity::list(TaskStatus* out, size_t outCapacity) {
    TaskEntry table[32];
    size_t n = buildTaskTable(table, 32);
    size_t written = 0;
    for (size_t i = 0; i < n && written < outCapacity; i++) {
        out[written++] = {table[i].label, table[i].on, table[i].isRf};
    }
    return written;
}

uint16_t activity::draw(M5Canvas& gfx, int16_t rightX, int16_t y) {
    TaskEntry table[32];
    size_t n = buildTaskTable(table, 32);

    int promCount = 0;
    const char* promOnly = nullptr;
    int bgCount = 0;
    const char* bgOnly = nullptr;
    for (size_t i = 0; i < n; i++) {
        if (!table[i].on) continue;
        if (table[i].isRf) {
            promCount++;
            promOnly = table[i].shortTag;
        } else {
            bgCount++;
            bgOnly = table[i].shortTag;
        }
    }

    char buf[14];
    uint16_t color;
    bool haveTag = true;
    if (promCount >= 2) {
        snprintf(buf, sizeof(buf), "RF:%d!", promCount);  // radio conflict — outranks everything
        color = theme::RED;
    } else if (promCount == 1 && bgCount == 0) {
        snprintf(buf, sizeof(buf), "RF:%s", promOnly);
        color = theme::AMBER;
    } else if (promCount == 1) {
        snprintf(buf, sizeof(buf), "RF:%s+%d", promOnly, bgCount);
        color = theme::AMBER;
    } else if (bgCount == 1) {
        snprintf(buf, sizeof(buf), "BG:%s", bgOnly);
        color = theme::CYAN;
    } else if (bgCount > 1) {
        snprintf(buf, sizeof(buf), "BG:%d", bgCount);
        color = theme::CYAN;
    } else {
        haveTag = false;  // nothing running in the background — leave the header clean
    }

    if (haveTag) {
        int16_t w = (int16_t)strlen(buf) * theme::GLYPH_W;
        int16_t x = rightX - w;
        if (x >= 0) {
            gfx.setTextColor(color, theme::BG);
            gfx.setCursor(x, y);
            gfx.print(buf);
        }
    }

    // Busy-header cue (Fase 37): escalate the header separator line's
    // color when THREE OR MORE background/promiscuous tasks are running
    // at once — the tag above only ever shows one name plus a count, an
    // easy thing to skim past; a colored strip spanning the whole header
    // is a stronger, harder-to-miss signal that a lot is going on right
    // now. Red matches an actual radio conflict (same threshold as the
    // RF:N! tag itself); amber otherwise; grey (normal) below threshold.
    constexpr int kBusyThreshold = 3;
    int totalActive = promCount + bgCount;
    if (totalActive >= kBusyThreshold) return (promCount >= 2) ? theme::RED : theme::AMBER;
    return theme::GREY;
}
