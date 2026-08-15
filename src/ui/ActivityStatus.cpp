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
#include <cstdio>
#include <cstring>

void activity::draw(M5Canvas& gfx, int16_t rightX, int16_t y) {
    // Every promiscuous-mode consumer in the firmware. Short tags keep the
    // header readable when one is active; the count is what matters when
    // more than one is (radio-callback conflict).
    const struct {
        bool on;
        const char* tag;
    } prom[] = {
        {g_arpSpoofManager.isRunning(), "ARP"},
        {g_deauthManager.isRunning(), "DTH"},
        {g_pmkidManager.isRunning(), "PMK"},
        {g_cdpLldpSniffer.isRunning(), "CDP"},
        {g_rogueDhcpDetector.isRunning(), "DHCP"},
        {g_passiveHostDiscovery.isRunning(), "PSV"},
        {g_beaconProbeSniffer.isRunning(), "BCN"},
        {g_deauthWatcher.isRunning(), "GRD"},
    };
    int promCount = 0;
    const char* promOnly = nullptr;
    for (const auto& p : prom) {
        if (p.on) {
            promCount++;
            promOnly = p.tag;
        }
    }

    // Everything else that keeps running after you leave its own screen:
    // war driving, the scanners, and the one-shot/audit sweeps. None of
    // these compete for the promiscuous callback, but all of them are
    // just as easy to forget about once you've navigated elsewhere.
    const struct {
        bool on;
        const char* tag;
    } bg[] = {
        {g_discoveryRunner.isRunning(), "ALL"},
        {g_wardrivingManager.isRunning(), "WD"},
        {g_scanManager.isRunning(), "SCN"},
        {g_portScanManager.isRunning(), "PORT"},
        {g_serviceEnumerator.isRunning(), "SVC"},
        {g_ssdpDiscovery.isRunning(), "UPNP"},
        {g_snmpSweep.isRunning(), "SNMP"},
        {g_dataStoreProbe.isRunning(), "DS"},
        {g_ldapProbe.isRunning(), "LDAP"},
        {g_ntlmHttpProbe.isRunning(), "NTLM"},
        {g_serviceAuditManager.isRunning(), "AUD"},
        {g_credAuditManager.isRunning(), "CRED"},
        {g_httpBruteforcer.isRunning(), "HTTP"},
        {g_smbCheck.isRunning(), "SMB"},
        {g_assessmentRunner.isRunning(), "ASSESS"},
        {g_evilTwinManager.isRunning(), "TWIN"},
    };
    int bgCount = 0;
    const char* bgOnly = nullptr;
    for (const auto& b : bg) {
        if (b.on) {
            bgCount++;
            bgOnly = b.tag;
        }
    }

    char buf[14];
    uint16_t color;
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
        return;  // nothing running in the background — leave the header clean
    }

    int16_t w = (int16_t)strlen(buf) * theme::GLYPH_W;
    int16_t x = rightX - w;
    if (x < 0) return;
    gfx.setTextColor(color, theme::BG);
    gfx.setCursor(x, y);
    gfx.print(buf);
}
