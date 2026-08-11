#include "HostListScreen.h"
#include "HostDetailScreen.h"
#include "WifiSetupScreen.h"
#include "../UiManager.h"
#include "../Theme.h"
#include "../Chrome.h"
#include "../../core/Config.h"
#include "../../core/Types.h"
#include "../../net/WifiManager.h"
#include "../../scan/ScanManager.h"
#include "../../storage/ResultStore.h"
#include <LittleFS.h>
#include <cstdio>

HostListScreen& HostListScreen::instance() {
    static HostListScreen s;
    return s;
}

void HostListScreen::onEnter() {
    rebuildAliveList();
}

void HostListScreen::rebuildAliveList() {
    _aliveIndices.clear();
    size_t n = g_scanManager.hostCount();
    HostInfo h;
    for (size_t i = 0; i < n; i++) {
        if (g_scanManager.getHost(i, h) && h.alive) _aliveIndices.push_back(i);
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
            break;
        case ScanEventType::HostChanged:
            if (ev.hostIndex >= 0) _aliveIndices.push_back((size_t)ev.hostIndex);
            break;
        case ScanEventType::ScanFinished:
            _scanFinishMs = millis();
            if (g_config.autoExportOnScanFinish) {
                bool okJson = ResultStore::exportJson(LittleFS, "/export.json");
                bool okCsv = ResultStore::exportCsv(LittleFS, "/export.csv");
                _statusLine = (okJson && okCsv) ? "auto-exported /export.json + .csv"
                                                 : "auto-export FAILED (see serial log)";
            }
            break;
        default:
            break;
    }
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
                bool okJson = ResultStore::exportJson(LittleFS, "/export.json");
                bool okCsv = ResultStore::exportCsv(LittleFS, "/export.csv");
                _statusLine = (okJson && okCsv) ? "exported /export.json + .csv"
                                                 : "export FAILED (see serial log)";
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

        gfx.setTextColor(theme::GREY, theme::BG);
        gfx.setCursor(4, gfx.height() - 9);
        gfx.print("DEL:back");
        return;
    }

    // Stat strip (also shown once finished, pinned at the final values,
    // until the next scan starts): "HOSTS FOUND: N" left, either the
    // live scan percentage or the final elapsed time right-aligned.
    uint32_t elapsedMs = (running ? millis() : _scanFinishMs) - _scanStartMs;
    uint32_t sec = elapsedMs / 1000;
    char timeBuf[8];
    snprintf(timeBuf, sizeof(timeBuf), "%02u:%02u", (unsigned)(sec / 60), (unsigned)(sec % 60));

    gfx.setTextColor(running ? theme::CYAN : theme::GREEN, theme::BG);
    gfx.setCursor(4, 18);
    gfx.print("HOSTS FOUND: ");
    gfx.print((unsigned)_aliveIndices.size());

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
    gfx.print("ENTER:detail E:export W:wifi DEL:back");
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

        uint16_t color = sel ? theme::CYAN : theme::riskColor(h.risk);
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
