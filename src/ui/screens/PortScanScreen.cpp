#include "PortScanScreen.h"
#include "../UiManager.h"
#include "../Theme.h"
#include "../../core/Config.h"
#include "../../scan/PortScanManager.h"

PortScanScreen& PortScanScreen::instance() {
    static PortScanScreen s;
    return s;
}

void PortScanScreen::onEnter() {
    _selected = 0;
}

bool PortScanScreen::isForThisHost() const {
    return g_portScanManager.target() == _target &&
           (g_portScanManager.isRunning() || g_portScanManager.hasScannedAnything());
}

void PortScanScreen::onScanEvent(const ScanNotification& ev) {
    if (ev.source != ScanSource::PortScan) return;  // not ours — see ScanSource in EventQueue.h
    // Nothing to do beyond re-reading PortScanManager state each
    // draw()/onKey() — the manager itself is the source of truth, this
    // screen has no local copy to keep in sync.
    (void)ev;
}

void PortScanScreen::onKey(UiKey key, char /*ch*/) {
    bool running = g_portScanManager.isRunning();

    if (!isForThisHost() && !running) {
        if (key == UiKey::Enter) {
            g_portScanManager.startScan(_target, g_config.portRangeStart, g_config.portRangeEnd);
        } else if (key == UiKey::Back) {
            g_ui.popScreen();
        }
        return;
    }

    size_t count = g_portScanManager.resultCount();
    switch (key) {
        case UiKey::Up:
            if (_selected > 0) _selected--;
            break;
        case UiKey::Down:
            if (_selected + 1 < count) _selected++;
            break;
        case UiKey::Enter:
            // Re-scan (only meaningful once idle — startScan() is a
            // no-op while already running).
            if (!running) g_portScanManager.startScan(_target, g_config.portRangeStart, g_config.portRangeEnd);
            break;
        case UiKey::Back:
            g_ui.popScreen();
            break;
        default:
            break;
    }
}

void PortScanScreen::draw(M5Canvas& gfx) {
    gfx.fillScreen(theme::BG);
    gfx.setTextSize(1);
    gfx.setTextColor(theme::GREEN_BRIGHT, theme::BG);
    gfx.setCursor(4, 4);
    gfx.print(">> PORT SCAN ");
    gfx.print(_target.toString());
    gfx.drawFastHLine(4, 15, gfx.width() - 8, theme::GREY);

    bool running = g_portScanManager.isRunning();
    bool relevant = isForThisHost();

    if (!relevant && !running) {
        gfx.setTextColor(theme::GREEN, theme::BG);
        gfx.setCursor(6, 24);
        gfx.print("range: ");
        gfx.print(g_config.portRangeStart);
        gfx.print("-");
        gfx.print(g_config.portRangeEnd);

        gfx.setTextColor(theme::MAGENTA, theme::BG);
        gfx.setCursor(6, 40);
        gfx.print("ENTER: start scan");

        gfx.setTextColor(theme::GREY, theme::BG);
        gfx.setCursor(4, gfx.height() - 9);
        gfx.print("DEL:back");
        return;
    }

    size_t count = g_portScanManager.resultCount();

    gfx.setTextColor(running ? theme::CYAN : theme::GREEN, theme::BG);
    gfx.setCursor(4, 18);
    gfx.print(running ? "scanning " : "done ");
    gfx.print(g_portScanManager.progressPct());
    gfx.print("%  open:");
    gfx.print((unsigned)count);

    drawResults(gfx, 28);

    gfx.setTextColor(theme::GREY, theme::BG);
    gfx.setCursor(4, gfx.height() - 9);
    gfx.print(running ? "DEL:back" : "ENTER:rescan  DEL:back");
}

void PortScanScreen::drawResults(M5Canvas& gfx, int16_t top) {
    constexpr int16_t kRowH = 10;
    constexpr int16_t kMaxRows = 10;

    size_t count = g_portScanManager.resultCount();
    size_t first = 0;
    if (_selected >= kMaxRows) first = _selected - kMaxRows + 1;

    PortResult r;
    for (size_t row = 0; row < kMaxRows; row++) {
        size_t i = first + row;
        if (i >= count) break;
        if (!g_portScanManager.getResult(i, r)) continue;

        int16_t y = top + (int16_t)row * kRowH;
        bool sel = (i == _selected);
        uint16_t rowBg = sel ? theme::GREEN_DIM : theme::BG;
        if (sel) gfx.fillRect(0, y, gfx.width(), kRowH, rowBg);

        bool risky = (r.port == 21 || r.port == 23 || r.port == 139 || r.port == 445 || r.port == 3389);
        uint16_t color = sel ? theme::GREEN_BRIGHT : (risky ? theme::AMBER : theme::GREEN);
        gfx.setTextColor(color, rowBg);
        gfx.setCursor(2, y + 1);

        String portStr = String(r.port);
        gfx.print(portStr);
        for (int p = portStr.length(); p < 6; p++) gfx.print(' ');

        String svc = r.service.length() ? r.service : "?";
        gfx.print(svc);
        for (int p = svc.length(); p < 12; p++) gfx.print(' ');

        String banner = r.banner;
        if (banner.length() > 20) banner = banner.substring(0, 20);
        gfx.print(banner);
    }
}
