#include "HostListScreen.h"
#include "HostDetailScreen.h"
#include "../UiManager.h"
#include "../Theme.h"
#include "../../core/Types.h"
#include "../../net/WifiManager.h"
#include "../../scan/ScanManager.h"

HostListScreen& HostListScreen::instance() {
    static HostListScreen s;
    return s;
}

void HostListScreen::onEnter() {
    _wifiConnectTriggered = false;
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
            break;
        case ScanEventType::HostChanged:
            if (ev.hostIndex >= 0) _aliveIndices.push_back((size_t)ev.hostIndex);
            break;
        default:
            break;
    }
}

void HostListScreen::update(uint32_t /*nowMs*/) {
    if (!g_wifi.isConnected() && !_wifiConnectTriggered) {
        _wifiConnectTriggered = true;
        g_wifi.beginConnect();
    }
}

void HostListScreen::onKey(UiKey key, char /*ch*/) {
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
        default:
            break;
    }
}

void HostListScreen::draw(M5Canvas& gfx) {
    gfx.fillScreen(theme::BG);
    gfx.setTextSize(1);
    gfx.setTextColor(theme::GREEN_BRIGHT, theme::BG);
    gfx.setCursor(4, 4);
    gfx.print(">> NETWORK SCAN");
    gfx.drawFastHLine(4, 15, gfx.width() - 8, theme::GREY);

    if (!g_wifi.isConnected()) {
        gfx.setTextColor(theme::CYAN, theme::BG);
        gfx.setCursor(6, 30);
        gfx.print("connecting to wifi...");
        gfx.setTextColor(theme::GREY, theme::BG);
        gfx.setCursor(6, 44);
        gfx.print("(check include/secrets.h)");
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

    // Progress line (also shown once finished, pinned at 100%, until the
    // next scan starts — harmless and reassures the user it's done).
    gfx.setTextColor(running ? theme::CYAN : theme::GREEN, theme::BG);
    gfx.setCursor(4, 18);
    gfx.print(running ? "scanning " : "done ");
    gfx.print(g_scanManager.progressPct());
    gfx.print("%  found:");
    gfx.print((unsigned)_aliveIndices.size());

    drawTable(gfx, 28);

    gfx.setTextColor(theme::GREY, theme::BG);
    gfx.setCursor(4, gfx.height() - 9);
    gfx.print("ENTER:detail  DEL:back");
}

void HostListScreen::drawTable(M5Canvas& gfx, int16_t top) {
    constexpr int16_t kRowH = 10;
    constexpr int16_t kMaxRows = 10;  // (135 - top - footer) / kRowH, rounded down

    size_t first = 0;
    if (_selected >= kMaxRows) first = _selected - kMaxRows + 1;

    HostInfo h;
    for (size_t row = 0; row < kMaxRows; row++) {
        size_t i = first + row;
        if (i >= _aliveIndices.size()) break;
        if (!g_scanManager.getHost(_aliveIndices[i], h)) continue;

        int16_t y = top + (int16_t)row * kRowH;
        bool sel = (i == _selected);
        uint16_t rowBg = sel ? theme::GREEN_DIM : theme::BG;
        if (sel) gfx.fillRect(0, y, gfx.width(), kRowH, rowBg);

        uint16_t color = sel ? theme::GREEN_BRIGHT : theme::riskColor(h.risk);
        gfx.setTextColor(color, rowBg);
        gfx.setCursor(2, y + 1);

        String ipStr = h.ip.toString();
        gfx.print(ipStr);
        for (int p = ipStr.length(); p < 16; p++) gfx.print(' ');

        gfx.print(deviceClassLabel(h.deviceClass));
        gfx.print(' ');

        String vendor = h.vendor.length() ? h.vendor : "-";
        if (vendor.length() > 16) vendor = vendor.substring(0, 16);
        gfx.print(vendor);
    }
}
