#include "ThreatsScreen.h"
#include "../UiManager.h"
#include "../Theme.h"
#include "../Chrome.h"
#include "../../core/Types.h"
#include "../../scan/ScanManager.h"
#include "../../scan/RogueDhcpDetector.h"
#include "../../scan/BeaconProbeSniffer.h"
#include "../../scan/DeauthWatcher.h"
#include "../../scan/SentinelManager.h"
#include "../../scan/PmkidSweepManager.h"
#include "../../scan/IotOtProbe.h"
#include <vector>

ThreatsScreen& ThreatsScreen::instance() {
    static ThreatsScreen s;
    return s;
}

void ThreatsScreen::onEnter() {
    _selected = 0;
}

void ThreatsScreen::onKey(UiKey key, char ch) {
    if (_showDetail) {
        _showDetail = false;
        return;
    }
    if (key == UiKey::Up) {
        if (_selected > 0) _selected--;
    } else if (key == UiKey::Down) {
        _selected++;  // clamped against the live count in draw()
    } else if (key == UiKey::Char && (ch == 'i' || ch == 'I')) {
        _showDetail = true;  // draw() no-ops it if there's nothing at _selected
    } else if (key == UiKey::Back) {
        g_ui.popScreen();
    }
}

namespace {
struct Finding {
    String text;
    uint16_t color;
};

// Re-derives the finding list from live data. Bounded to keep the
// per-draw cost predictable; the most severe categories are appended
// first so a truncated list still shows the worst.
void collectFindings(std::vector<Finding>& out) {
    constexpr size_t kMax = 40;

    size_t n = g_scanManager.hostCount();
    HostInfo h;
    for (size_t i = 0; i < n && out.size() < kMax; i++) {
        if (!g_scanManager.getHost(i, h) || !h.alive) continue;

        if (h.credVulnerable) {
            out.push_back({h.ip.toString() + " default/weak creds", theme::RED});
            continue;  // strongest finding for this host, don't pile on
        }
        if (h.vulnNote.length()) {
            String note = h.vulnNote;
            if (note.length() > 22) note = note.substring(0, 22);
            out.push_back({h.ip.toString() + " " + note, theme::RED});
            continue;
        }
        bool telnet = false, ftp = false;
        for (const auto& p : h.ports) {
            if (p.service == "telnet" || p.port == 23) telnet = true;
            if (p.service == "ftp" || p.port == 21) ftp = true;
        }
        if (telnet || ftp) {
            out.push_back({h.ip.toString() + (telnet ? " telnet open" : " ftp open"), theme::AMBER});
        }
    }

    size_t rc = g_rogueDhcpDetector.sightingCount();
    RogueDhcpDetector::Sighting s;
    for (size_t i = 0; i < rc && out.size() < kMax; i++) {
        if (g_rogueDhcpDetector.getSighting(i, s) && s.suspicious) {
            out.push_back({s.serverIp.toString() + " rogue DHCP?", theme::RED});
        }
    }

    // WPS still accepting PIN attempts (enabled, not locked) - a real
    // exposure (Reaver/pixie-dust-style attacks target exactly this),
    // surfaced here even though BEACON/PROBE INTEL has to have been run
    // at least once this session to have seen it at all.
    size_t apCount = g_beaconProbeSniffer.apCount();
    BeaconProbeSniffer::ApBeacon ap;
    for (size_t i = 0; i < apCount && out.size() < kMax; i++) {
        if (g_beaconProbeSniffer.getAp(i, ap) && ap.wpsEnabled && !ap.wpsLocked) {
            out.push_back({(ap.hidden ? String("<hidden>") : ap.ssid) + " WPS unlocked", theme::AMBER});
        }
    }

    // Deauth/disassoc flood in progress against a BSSID - GUARD MODE has
    // to be (or have been) running to catch this; see scan/DeauthWatcher.h.
    size_t incCount = g_deauthWatcher.incidentCount();
    DeauthWatcher::Incident inc;
    for (size_t i = 0; i < incCount && out.size() < kMax; i++) {
        if (g_deauthWatcher.getIncident(i, inc) && inc.flooding) {
            out.push_back({inc.bssid + " deauth flood", theme::RED});
        }
    }

    // SENTINEL MODE's own event log - new/gone devices on the watched
    // network, plus deauth floods it caught itself (its own folded-in
    // GUARD MODE logic, a separate detector from the one above - see
    // SentinelManager.h). Has to be (or have been) running this session.
    size_t evCount = g_sentinelManager.eventLogCount();
    SentinelManager::Event ev;
    for (size_t i = 0; i < evCount && out.size() < kMax; i++) {
        if (!g_sentinelManager.getEvent(i, ev)) continue;
        switch (ev.kind) {
            case SentinelManager::EventKind::NewDevice:
                out.push_back({ev.ip.toString() + " new on network", theme::AMBER});
                break;
            case SentinelManager::EventKind::DeviceGone:
                out.push_back({(ev.hostname.length() ? ev.hostname : ev.ip.toString()) + " went dark",
                                theme::AMBER});
                break;
            case SentinelManager::EventKind::DeauthFlood:
                out.push_back({ev.mac + " deauth flood (sentinel)", theme::RED});
                break;
        }
    }

    // IOT/OT SWEEP: unauthenticated access on protocols found on IoT/OT
    // segments (Fase 39). OT protocols (Modbus/BACnet/DNP3) rank
    // Critical/RED - none of the three has ANY authentication concept
    // in the protocol itself, so exposure here is never "someone left
    // auth off", it's the protocol design being reachable outside a
    // properly segmented OT network. IoT protocols (MQTT/CoAP) rank
    // Warning/AMBER since those DO have an auth mechanism that was left
    // disabled - a real but lesser finding than an inherently
    // unauthenticatable OT device answering on the LAN.
    size_t iotCount = g_iotOtProbe.count();
    IotOtProbe::Finding iotFind;
    for (size_t i = 0; i < iotCount && out.size() < kMax; i++) {
        if (!g_iotOtProbe.get(i, iotFind) || !iotFind.noAuth) continue;
        bool isOt = (iotFind.service == "modbus" || iotFind.service == "bacnet" || iotFind.service == "dnp3");
        out.push_back(
            {iotFind.ip.toString() + " " + iotFind.service + " no-auth", isOt ? theme::RED : theme::AMBER});
    }

    // PMKID SWEEP: not a threat by itself (the user ran it deliberately
    // against their own APs), just a status note so a completed sweep's
    // headline result is visible from the same rollup as everything
    // else, without having to remember to check PMKID SWEEP directly.
    if (g_pmkidSweepManager.hitCount() > 0) {
        out.push_back({String((unsigned)g_pmkidSweepManager.hitCount()) + " PMKID(s) captured this session",
                        theme::CYAN});
    }
}
}  // namespace

void ThreatsScreen::draw(M5Canvas& gfx) {
    std::vector<Finding> findings;
    collectFindings(findings);

    if (_selected >= findings.size()) _selected = findings.empty() ? 0 : findings.size() - 1;

    if (_showDetail) {
        if (_selected < findings.size()) {
            chrome::drawDetailOverlay(gfx, "THREAT FINDING", findings[_selected].text);
        }
        return;
    }

    gfx.fillScreen(theme::BG);
    chrome::drawHeader(gfx, "THREATS");

    gfx.setTextColor(findings.empty() ? theme::GREEN : theme::RED, theme::BG);
    gfx.setCursor(6, 18);
    gfx.print("findings: ");
    gfx.print((unsigned)findings.size());

    if (findings.empty()) {
        gfx.setTextColor(theme::GREY, theme::BG);
        gfx.setCursor(6, 40);
        gfx.print("nothing flagged (yet).");
        gfx.setCursor(6, 52);
        gfx.print("run NETWORK SCAN / audits to");
        gfx.setCursor(6, 64);
        gfx.print("populate this view.");
    } else {
        int16_t top = 28;
        gfx.setTextColor(theme::GREY, theme::BG);
        gfx.drawFastHLine(4, top, gfx.width() - 8, theme::GREY);

        constexpr int16_t kRowH = 11;
        constexpr size_t kMaxRows = 8;
        size_t first = 0;
        if (_selected >= kMaxRows) first = _selected - kMaxRows + 1;

        for (size_t row = 0; row < kMaxRows; row++) {
            size_t i = first + row;
            if (i >= findings.size()) break;
            int16_t y = top + 3 + (int16_t)row * kRowH;
            bool sel = (i == _selected);
            uint16_t rowBg = sel ? theme::PANEL_BG : theme::BG;
            if (sel) gfx.fillRect(0, y - 1, gfx.width(), kRowH, rowBg);
            gfx.setTextColor(sel ? theme::CYAN : findings[i].color, rowBg);
            gfx.setCursor(6, y);
            String t = findings[i].text;
            if (t.length() > 38) t = t.substring(0, 38);
            gfx.print(t);
        }

        chrome::drawScrollMarkers(gfx, top + 3, top + 3 + (int16_t)kMaxRows * kRowH, first > 0,
                                   (first + kMaxRows) < findings.size());
    }

    gfx.setTextColor(theme::GREY, theme::BG);
    gfx.setCursor(4, gfx.height() - 9);
    gfx.print(findings.empty() ? "DEL:back" : "I:full text  DEL:back");
}
