#include "FindingStore.h"

#include <Arduino.h>  // millis()
#include <algorithm>

#include "../core/Types.h"
#include "ScanManager.h"
#include "RogueDhcpDetector.h"
#include "BeaconProbeSniffer.h"
#include "DeauthWatcher.h"
#include "SentinelManager.h"
#include "PmkidSweepManager.h"
#include "IotOtProbe.h"

FindingStore g_findings;

void FindingStore::begin() {
    _mutex = xSemaphoreCreateMutex();
}

void FindingStore::add(ScanSource source, FindingCategory category, RiskLevel severity, const IPAddress& host,
                       const String& title, const String& detail) {
    if (!_mutex || xSemaphoreTake(_mutex, pdMS_TO_TICKS(200)) != pdTRUE) return;

    // Dedup by (source, host, title): a repeated observation just
    // refreshes the timestamp/detail and (only ever upward) the severity,
    // so a finding that later proves worse escalates but never silently
    // downgrades — same "escalate-only" rule ScanManager uses for a
    // host's risk level.
    for (auto& f : _pushed) {
        if (f.source == source && f.host == host && f.title == title) {
            f.whenMs = millis();
            if (detail.length()) f.detail = detail;
            if ((uint8_t)severity > (uint8_t)f.severity) f.severity = severity;
            xSemaphoreGive(_mutex);
            return;
        }
    }

    if (_pushed.size() < kMaxPushed) {
        Finding f;
        f.source = source;
        f.category = category;
        f.severity = severity;
        f.host = host;
        f.title = title;
        f.detail = detail.length() ? detail : title;
        f.whenMs = millis();
        _pushed.push_back(f);
    }

    xSemaphoreGive(_mutex);
}

void FindingStore::clear() {
    if (!_mutex || xSemaphoreTake(_mutex, pdMS_TO_TICKS(200)) != pdTRUE) return;
    _pushed.clear();
    xSemaphoreGive(_mutex);
}

size_t FindingStore::pushedCount() const {
    if (!_mutex || xSemaphoreTake(_mutex, pdMS_TO_TICKS(100)) != pdTRUE) return 0;
    size_t n = _pushed.size();
    xSemaphoreGive(_mutex);
    return n;
}

namespace {

// Append a finding to `out` unless an equivalent one (same host+title)
// is already there — the DERIVED pass and the PUSHED pass can legitimately
// surface the same thing (e.g. a module pushed "no-auth" and the host
// table also reflects it), and the rollup should show it once.
void mergeUnique(std::vector<Finding>& out, const Finding& f) {
    for (const auto& e : out) {
        if (e.host == f.host && e.title == f.title) return;
    }
    out.push_back(f);
}

Finding mk(ScanSource src, FindingCategory cat, RiskLevel sev, const IPAddress& host, const String& title,
           const String& detail = String()) {
    Finding f;
    f.source = src;
    f.category = cat;
    f.severity = sev;
    f.host = host;
    f.title = title;
    f.detail = detail.length() ? detail : title;
    f.whenMs = millis();
    return f;
}

// DERIVED findings: live properties of ScanManager's host table. Moved
// here verbatim (in behavior) from ThreatsScreen's old collectFindings so
// THREATS, the report and the export share one definition. Kept host-
// derived rather than pushed so a re-scan that clears a port also clears
// the finding, with no stale push to reconcile.
void deriveFromHostTable(std::vector<Finding>& out) {
    size_t n = g_scanManager.hostCount();
    HostInfo h;
    for (size_t i = 0; i < n; i++) {
        if (!g_scanManager.getHost(i, h) || !h.alive) continue;

        if (h.credVulnerable) {
            mergeUnique(out, mk(ScanSource::CredAudit, FindingCategory::Weakness, RiskLevel::Critical, h.ip,
                                h.ip.toString() + " default/weak creds",
                                h.credNote.length() ? (h.ip.toString() + " default/weak creds: " + h.credNote)
                                                     : String()));
            continue;  // strongest finding for this host, don't pile on
        }
        if (h.vulnNote.length()) {
            String note = h.vulnNote;
            if (note.length() > 22) note = note.substring(0, 22);
            mergeUnique(out, mk(ScanSource::Discovery, FindingCategory::Weakness, RiskLevel::Critical, h.ip,
                                h.ip.toString() + " " + note, h.ip.toString() + " vuln banner: " + h.vulnNote));
            continue;
        }
        bool telnet = false, ftp = false, smb = false;
        for (const auto& p : h.ports) {
            if (p.service == "telnet" || p.port == 23) telnet = true;
            if (p.service == "ftp" || p.port == 21) ftp = true;
            if (p.service == "smb" || p.service == "netbios-ssn" || p.port == 445 || p.port == 139) smb = true;
        }
        // telnet/ftp and smb are independent exposures on the same host —
        // no `continue`, both can surface (matching the old report, which
        // listed them as separate findings).
        if (telnet || ftp) {
            mergeUnique(out, mk(ScanSource::Discovery, FindingCategory::Exposure, RiskLevel::Warning, h.ip,
                                h.ip.toString() + (telnet ? " telnet open" : " ftp open"),
                                h.ip.toString() + " plaintext service exposed - credentials travel unencrypted"));
        }
        if (smb) {
            mergeUnique(out, mk(ScanSource::Smb, FindingCategory::Exposure, RiskLevel::Warning, h.ip,
                                h.ip.toString() + " SMB/NetBIOS exposed",
                                h.ip.toString() + " SMB/NetBIOS exposed - check share ACLs and signing"));
        }
    }
}

// DERIVED findings from the standing background detectors — the six
// managers THREATS used to reach into directly. Same logic, one home.
void deriveFromDetectors(std::vector<Finding>& out) {
    size_t rc = g_rogueDhcpDetector.sightingCount();
    RogueDhcpDetector::Sighting s;
    for (size_t i = 0; i < rc; i++) {
        if (g_rogueDhcpDetector.getSighting(i, s) && s.suspicious) {
            mergeUnique(out, mk(ScanSource::RogueDhcp, FindingCategory::Threat, RiskLevel::Critical, s.serverIp,
                                s.serverIp.toString() + " rogue DHCP?",
                                s.serverIp.toString() + " DHCP server differs from the in-use gateway"));
        }
    }

    size_t apCount = g_beaconProbeSniffer.apCount();
    BeaconProbeSniffer::ApBeacon ap;
    for (size_t i = 0; i < apCount; i++) {
        if (g_beaconProbeSniffer.getAp(i, ap) && ap.wpsEnabled && !ap.wpsLocked) {
            mergeUnique(out, mk(ScanSource::BeaconProbe, FindingCategory::Weakness, RiskLevel::Warning,
                                IPAddress((uint32_t)0),
                                (ap.hidden ? String("<hidden>") : ap.ssid) + " WPS unlocked",
                                "WPS enabled and unlocked - vulnerable to Reaver/pixie-dust PIN attacks"));
        }
    }

    size_t incCount = g_deauthWatcher.incidentCount();
    DeauthWatcher::Incident inc;
    for (size_t i = 0; i < incCount; i++) {
        if (g_deauthWatcher.getIncident(i, inc) && inc.flooding) {
            mergeUnique(out, mk(ScanSource::DeauthWatch, FindingCategory::Threat, RiskLevel::Critical,
                                IPAddress((uint32_t)0), inc.bssid + " deauth flood",
                                inc.bssid + " deauth/disassoc flood in progress"));
        }
    }

    size_t evCount = g_sentinelManager.eventLogCount();
    SentinelManager::Event ev;
    for (size_t i = 0; i < evCount; i++) {
        if (!g_sentinelManager.getEvent(i, ev)) continue;
        switch (ev.kind) {
            case SentinelManager::EventKind::NewDevice:
                mergeUnique(out, mk(ScanSource::Sentinel, FindingCategory::Threat, RiskLevel::Warning, ev.ip,
                                    ev.ip.toString() + " new on network",
                                    ev.ip.toString() + " never seen in this network's baseline"));
                break;
            case SentinelManager::EventKind::DeviceGone:
                mergeUnique(out, mk(ScanSource::Sentinel, FindingCategory::Threat, RiskLevel::Warning, ev.ip,
                                    (ev.hostname.length() ? ev.hostname : ev.ip.toString()) + " went dark"));
                break;
            case SentinelManager::EventKind::DeauthFlood:
                mergeUnique(out, mk(ScanSource::Sentinel, FindingCategory::Threat, RiskLevel::Critical,
                                    IPAddress((uint32_t)0), ev.mac + " deauth flood (sentinel)"));
                break;
        }
    }

    size_t iotCount = g_iotOtProbe.count();
    IotOtProbe::Finding iotFind;
    for (size_t i = 0; i < iotCount; i++) {
        if (!g_iotOtProbe.get(i, iotFind) || !iotFind.noAuth) continue;
        bool isOt = (iotFind.service == "modbus" || iotFind.service == "bacnet" || iotFind.service == "dnp3");
        mergeUnique(out, mk(ScanSource::IotOt, FindingCategory::Exposure,
                            isOt ? RiskLevel::Critical : RiskLevel::Warning, iotFind.ip,
                            iotFind.ip.toString() + " " + iotFind.service + " no-auth"));
    }

    if (g_pmkidSweepManager.hitCount() > 0) {
        mergeUnique(out, mk(ScanSource::PmkidSweep, FindingCategory::Recon, RiskLevel::Ok,
                            IPAddress((uint32_t)0),
                            String((unsigned)g_pmkidSweepManager.hitCount()) + " PMKID(s) captured this session"));
    }
}

}  // namespace

void FindingStore::buildRollup(std::vector<Finding>& out) const {
    out.clear();

    // Pushed findings first (snapshot under the lock), then the two
    // derived passes fold in around them via mergeUnique.
    if (_mutex && xSemaphoreTake(_mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
        for (const auto& f : _pushed) out.push_back(f);
        xSemaphoreGive(_mutex);
    }

    deriveFromHostTable(out);
    deriveFromDetectors(out);

    // Most-severe-first; ties broken by most-recent-first so a fresh
    // observation surfaces above an older one of equal severity.
    std::stable_sort(out.begin(), out.end(), [](const Finding& a, const Finding& b) {
        if (a.severity != b.severity) return (uint8_t)a.severity > (uint8_t)b.severity;
        return a.whenMs > b.whenMs;
    });

    if (out.size() > kRollupMax) out.resize(kRollupMax);
}
