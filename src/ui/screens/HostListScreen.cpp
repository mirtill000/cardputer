#include "HostListScreen.h"
#include "HostDetailScreen.h"
#include "WifiSetupScreen.h"
#include "DiscoveryMenuScreen.h"
#include "SearchScreen.h"
#include "TargetRangeScreen.h"
#include "QrShareScreen.h"
#include "../UiManager.h"
#include "../Theme.h"
#include "../Chrome.h"
#include "../../core/Config.h"
#include "../../core/Types.h"
#include "../../net/WifiManager.h"
#include "../../scan/ScanManager.h"
#include "../../storage/ResultStore.h"
#include "../../storage/ReportGenerator.h"
#include "../../storage/ScanHistory.h"
#include "../../storage/SdCard.h"
#include "../../storage/NetrunnerPaths.h"
#include <cstdio>

HostListScreen& HostListScreen::instance() {
    static HostListScreen s;
    return s;
}

void HostListScreen::onEnter() {
    rebuildAliveList();
}

bool HostListScreen::matchesFilter(const HostInfo& h) const {
    switch (_filter) {
        case Filter::Risky: return h.risk != RiskLevel::Ok || h.credVulnerable;
        case Filter::WithPorts: return !h.ports.empty();
        default: return true;
    }
}

void HostListScreen::rebuildAliveList() {
    _aliveIndices.clear();
    size_t n = g_scanManager.hostCount();
    HostInfo h;
    for (size_t i = 0; i < n; i++) {
        if (g_scanManager.getHost(i, h) && h.alive && matchesFilter(h)) _aliveIndices.push_back(i);
    }
    if (_selected >= _aliveIndices.size()) {
        _selected = _aliveIndices.empty() ? 0 : _aliveIndices.size() - 1;
    }
}

void HostListScreen::onScanEvent(const ScanNotification& ev) {
    if (ev.source != ScanSource::Discovery) return;  // not ours — see ScanSource in EventQueue.h
    switch (ev.type) {
        case ScanEventType::ScanStarted:
            _aliveIndices.clear();
            _selected = 0;
            _statusLine = "";
            _scanStartMs = millis();
            _newHostIps.clear();
            _neverSeenIps.clear();
            break;
        case ScanEventType::HostChanged:
            if (ev.hostIndex >= 0) _aliveIndices.push_back((size_t)ev.hostIndex);
            break;
        case ScanEventType::ScanFinished: {
            _scanFinishMs = millis();

            fs::FS& fs = sdcard::exportFs();

            // Known-device baseline: built from PRIOR scans of this
            // exact network, before this scan's own snapshot gets
            // saved below - so it can never trivially satisfy its own
            // baseline. Stronger than _newHostIps (which only looks one
            // scan back): a host missing here has never appeared in any
            // of the last kMaxEntries scans of this network, not just
            // the immediately previous one.
            std::vector<String> knownMacs;
            ScanHistory::loadKnownMacs(fs, g_wifi.currentSsid(), knownMacs);
            _neverSeenIps.clear();
            {
                size_t n = g_scanManager.hostCount();
                HostInfo h;
                for (size_t i = 0; i < n; i++) {
                    if (!g_scanManager.getHost(i, h) || !h.alive || !h.macKnown) continue;
                    String mac = macToString(h.mac);
                    bool known = false;
                    for (const auto& m : knownMacs) {
                        if (m == mac) {
                            known = true;
                            break;
                        }
                    }
                    if (!known) _neverSeenIps.push_back(h.ip);
                }
            }

            ScanHistory::saveSnapshot(fs);
            ScanHistory::diffNewHosts(fs, _newHostIps);

            if (g_config.autoExportOnScanFinish) {
                String base = netrunner::reportBase(fs, g_wifi.currentSsid());
                bool okJson = ResultStore::exportJson(fs, (base + ".json").c_str());
                bool okCsv = ResultStore::exportCsv(fs, (base + ".csv").c_str());
                _statusLine = (okJson && okCsv)
                                  ? (String("auto-exported to /netrunner (") + sdcard::exportFsLabel() + ")")
                                  : "auto-export FAILED (see serial log)";
            }
            break;
        }
        default:
            break;
    }
}

bool HostListScreen::isNewHost(const IPAddress& ip) const {
    for (const auto& newIp : _newHostIps) {
        if (newIp == ip) return true;
    }
    return false;
}

bool HostListScreen::isNeverSeenBefore(const IPAddress& ip) const {
    for (const auto& p : _neverSeenIps) {
        if (p == ip) return true;
    }
    return false;
}

void HostListScreen::onKey(UiKey key, char ch) {
    // Always available, connected or not — WifiSetupScreen owns
    // actually establishing a connection (see main.cpp: it's kicked off
    // once at boot with any saved credentials); this screen only ever
    // reflects status and offers a way there.
    if (key == UiKey::Char && (ch == 'w' || ch == 'W')) {
        g_ui.pushScreen(&WifiSetupScreen::instance());
        return;
    }
    // All the network-discovery tools now live under one submenu, reached
    // from here — see DiscoveryMenuScreen. Available in every state (each
    // tool handles its own prerequisites like "needs a scan first").
    if (key == UiKey::Char && (ch == 'd' || ch == 'D')) {
        g_ui.pushScreen(&DiscoveryMenuScreen::instance());
        return;
    }
    if (key == UiKey::Char && (ch == 's' || ch == 'S')) {
        g_ui.pushScreen(&SearchScreen::instance());
        return;
    }
    if (key == UiKey::Char && (ch == 't' || ch == 'T')) {
        g_ui.pushScreen(&TargetRangeScreen::instance());
        return;
    }
    if (key == UiKey::Char && (ch == 'q' || ch == 'Q')) {
        g_ui.pushScreen(&QrShareScreen::instance());
        return;
    }

    if (!g_wifi.isConnected()) {
        if (key == UiKey::Back) g_ui.popScreen();
        return;
    }

    bool running = g_scanManager.isRunning();

    if (_aliveIndices.empty() && !running) {
        // idle, connected, nothing found yet (or previous scan found nothing)
        if (key == UiKey::Enter) {
            g_scanManager.startDiscoveryScan();
        } else if (key == UiKey::Back) {
            g_ui.popScreen();
        }
        return;
    }

    switch (key) {
        case UiKey::Up:
            if (_selected > 0) _selected--;
            break;
        case UiKey::Down:
            if (_selected + 1 < _aliveIndices.size()) _selected++;
            break;
        case UiKey::Enter:
            if (!_aliveIndices.empty()) {
                HostDetailScreen::instance().setHostIndex(_aliveIndices[_selected]);
                g_ui.pushScreen(&HostDetailScreen::instance());
            }
            break;
        case UiKey::Back:
            g_ui.popScreen();
            break;
        case UiKey::Char:
            if (ch == 'e' || ch == 'E') {
                fs::FS& fs = sdcard::exportFs();
                String base = netrunner::reportBase(fs, g_wifi.currentSsid());
                bool okJson = ResultStore::exportJson(fs, (base + ".json").c_str());
                bool okCsv = ResultStore::exportCsv(fs, (base + ".csv").c_str());
                _statusLine = (okJson && okCsv) ? (String("exported to /netrunner (") + sdcard::exportFsLabel() + ")")
                                                 : "export FAILED (see serial log)";
            } else if (ch == 'r' || ch == 'R') {
                fs::FS& fs = sdcard::exportFs();
                String path = netrunner::reportBase(fs, g_wifi.currentSsid()) + ".html";
                bool ok = ReportGenerator::generate(fs, path.c_str());
                _statusLine = ok ? (String("report saved to /netrunner (") + sdcard::exportFsLabel() + ")")
                                 : "report FAILED (see serial log)";
            } else if (ch == 'f' || ch == 'F') {
                _filter = (_filter == Filter::All)     ? Filter::Risky
                          : (_filter == Filter::Risky) ? Filter::WithPorts
                                                       : Filter::All;
                _selected = 0;
                rebuildAliveList();
            }
            break;
        default:
            break;
    }
}

void HostListScreen::draw(M5Canvas& gfx) {
    gfx.fillScreen(theme::BG);
    chrome::drawHeader(gfx, "NETWORK SCAN");

    if (!g_wifi.isConnected()) {
        gfx.setTextColor(theme::CYAN, theme::BG);
        gfx.setCursor(6, 30);
        if (g_wifi.hasSavedCredentials()) {
            gfx.print("connecting to ");
            gfx.print(g_wifi.savedSsid());
            gfx.print("...");
        } else {
            gfx.setTextColor(theme::AMBER, theme::BG);
            gfx.print("no network configured");
        }
        gfx.setTextColor(theme::MAGENTA, theme::BG);
        gfx.setCursor(6, 44);
        gfx.print("W: wifi setup");
        gfx.setTextColor(theme::GREY, theme::BG);
        gfx.setCursor(4, gfx.height() - 9);
        gfx.print("DEL:back");
        return;
    }

    bool running = g_scanManager.isRunning();

    if (_aliveIndices.empty() && !running) {
        gfx.setTextColor(theme::GREEN, theme::BG);
        gfx.setCursor(6, 22);
        gfx.print("subnet: ");
        gfx.print(g_wifi.networkAddress().toString());
        gfx.setCursor(6, 32);
        gfx.print("hosts to probe: ");
        gfx.print(g_wifi.hostCount());
        gfx.setCursor(6, 42);
        gfx.print("gateway: ");
        gfx.print(g_wifi.gatewayIP().toString());

        gfx.setTextColor(theme::MAGENTA, theme::BG);
        gfx.setCursor(6, 60);
        gfx.print("ENTER: start scan");

        // Row-color legend (consistent semantics across the app): while
        // there's room on the idle screen, spell out what the table colors
        // mean so the populated view doesn't need to.
        gfx.setCursor(6, 78);
        gfx.setTextColor(theme::RED, theme::BG);
        gfx.print("red");
        gfx.setTextColor(theme::GREY, theme::BG);
        gfx.print("=never-seen  ");
        gfx.setTextColor(theme::MAGENTA, theme::BG);
        gfx.print("mag");
        gfx.setTextColor(theme::GREY, theme::BG);
        gfx.print("=new");
        gfx.setCursor(6, 88);
        gfx.setTextColor(theme::AMBER, theme::BG);
        gfx.print("amber");
        gfx.setTextColor(theme::GREY, theme::BG);
        gfx.print("=warning  ");
        gfx.setTextColor(theme::GREEN, theme::BG);
        gfx.print("green");
        gfx.setTextColor(theme::GREY, theme::BG);
        gfx.print("=ok");

        gfx.setTextColor(theme::GREY, theme::BG);
        gfx.setCursor(4, gfx.height() - 9);
        gfx.print("ENTER:scan D:discovery W:wifi ?:help");
        return;
    }

    // Stat strip (also shown once finished, pinned at the final values,
    // until the next scan starts): "HOSTS FOUND: N" left, either the
    // live scan percentage or the final elapsed time right-aligned.
    uint32_t elapsedMs = (running ? millis() : _scanFinishMs) - _scanStartMs;
    uint32_t sec = elapsedMs / 1000;
    char timeBuf[16];  // "MM:SS" is 6 bytes, but minutes isn't clamped - room for a much longer scan
    snprintf(timeBuf, sizeof(timeBuf), "%02u:%02u", (unsigned)(sec / 60), (unsigned)(sec % 60));

    gfx.setTextColor(running ? theme::CYAN : theme::GREEN, theme::BG);
    gfx.setCursor(4, 18);
    gfx.print("HOSTS: ");
    gfx.print((unsigned)_aliveIndices.size());
    if (_filter != Filter::All) {
        gfx.setTextColor(theme::MAGENTA, theme::BG);
        gfx.print(_filter == Filter::Risky ? " [risky]" : " [ports]");
        gfx.setTextColor(running ? theme::CYAN : theme::GREEN, theme::BG);
    }
    if (!running) {
        // "+N" (magenta) = new since the immediately previous scan;
        // "!N" (red) = never seen in ANY past scan of this network -
        // a stronger claim, see ScanHistory::loadKnownMacs(). Kept to
        // symbols rather than words - the right-aligned SCAN TIME text
        // shares this same line and there isn't room for both spelled
        // out.
        if (!_newHostIps.empty()) {
            gfx.setTextColor(theme::MAGENTA, theme::BG);
            gfx.print(" +");
            gfx.print((unsigned)_newHostIps.size());
        }
        if (!_neverSeenIps.empty()) {
            gfx.setTextColor(theme::RED, theme::BG);
            gfx.print(" !");
            gfx.print((unsigned)_neverSeenIps.size());
        }
    }

    String rightStat = running ? (String(g_scanManager.progressPct()) + "%") : ("SCAN TIME " + String(timeBuf));
    int16_t rightX = gfx.width() - (int16_t)rightStat.length() * theme::GLYPH_W - 4;
    gfx.setCursor(rightX, 18);
    gfx.print(rightStat);

    drawTable(gfx, 28);

    if (_statusLine.length()) {
        gfx.fillRect(0, gfx.height() - 19, gfx.width(), 9, theme::BG);
        gfx.setTextColor(theme::CYAN, theme::BG);
        gfx.setCursor(4, gfx.height() - 19);
        gfx.print(_statusLine);
    }

    gfx.setTextColor(theme::GREY, theme::BG);
    gfx.setCursor(4, gfx.height() - 9);
    gfx.print("ENTER:host D:disc F:filt E:exp ?:help");
}

void HostListScreen::drawTable(M5Canvas& gfx, int16_t top) {
    // Bordered panel with a column-header row, matching PORT MAPPING's
    // framed-table look — see README Fase 9. Leaves room below the box
    // for the optional status line (shown after an export) plus the
    // footer hint row at the bottom of the screen — see draw().
    constexpr int16_t kBoxBottom = 113;
    constexpr int16_t kColIp = 4;
    constexpr int16_t kColType = 4 + 16 * theme::GLYPH_W;    // "255.255.255.255" + gap
    constexpr int16_t kColVendor = kColType + 8 * theme::GLYPH_W;  // "UNKNOWN" + gap
    constexpr int16_t kVendorMaxChars = 14;

    gfx.drawRect(2, top, gfx.width() - 4, kBoxBottom - top, theme::GREY);

    gfx.setTextColor(theme::MAGENTA, theme::BG);
    gfx.setCursor(kColIp, top + 2);
    gfx.print("IP");
    gfx.setCursor(kColType, top + 2);
    gfx.print("TYPE");
    gfx.setCursor(kColVendor, top + 2);
    gfx.print("VENDOR");

    constexpr int16_t kHeaderH = 11;
    gfx.drawFastHLine(3, top + kHeaderH, gfx.width() - 6, theme::GREY);

    constexpr int16_t kRowH = 10;
    int16_t rowsTop = top + kHeaderH + 2;
    int16_t kMaxRows = (kBoxBottom - rowsTop) / kRowH;

    size_t first = 0;
    if ((int16_t)_selected >= kMaxRows) first = _selected - (size_t)kMaxRows + 1;

    HostInfo h;
    for (int16_t row = 0; row < kMaxRows; row++) {
        size_t i = first + (size_t)row;
        if (i >= _aliveIndices.size()) break;
        if (!g_scanManager.getHost(_aliveIndices[i], h)) continue;

        int16_t y = rowsTop + row * kRowH;
        bool sel = (i == _selected);
        uint16_t rowBg = sel ? theme::PANEL_BG : theme::BG;
        if (sel) gfx.fillRect(3, y, gfx.width() - 6, kRowH, rowBg);

        // Priority when several signals apply to the same row: never-
        // seen-on-this-network-before outranks merely-new-since-last-
        // scan (a stronger, more specific claim - see isNeverSeenBefore
        // vs isNewHost), which outranks the generic risk-level color.
        bool neverSeen = isNeverSeenBefore(h.ip);
        bool isNew = isNewHost(h.ip);
        uint16_t color = sel ? theme::CYAN : (neverSeen ? theme::RED : (isNew ? theme::MAGENTA : theme::riskColor(h.risk)));
        gfx.setTextColor(color, rowBg);

        gfx.setCursor(kColIp, y + 1);
        gfx.print(h.ip.toString());

        gfx.setCursor(kColType, y + 1);
        gfx.print(deviceClassLabel(h.deviceClass));

        String vendor = h.vendor.length() ? h.vendor : "-";
        if (vendor.length() > kVendorMaxChars) vendor = vendor.substring(0, kVendorMaxChars);
        gfx.setCursor(kColVendor, y + 1);
        gfx.print(vendor);
    }
}
