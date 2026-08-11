#include "PortScanScreen.h"
#include "../UiManager.h"
#include "../Theme.h"
#include "../Chrome.h"
#include "../../core/Config.h"
#include "../../scan/PortScanManager.h"

namespace {
// Two real segments — open vs. rest-of-configured-range — not a
// fabricated "filtered/closed" breakdown: telling those two apart
// would need raw sockets we don't have (see README). fillArc() angle
// convention: 0deg = 12 o'clock, increasing clockwise (LovyanGFX gauge
// convention).
void drawOpenPortsDonut(M5Canvas& gfx, int16_t cx, int16_t cy, int16_t rOuter, int16_t rInner, float openFrac) {
    if (openFrac < 0.f) openFrac = 0.f;
    if (openFrac > 1.f) openFrac = 1.f;
    float openDeg = openFrac * 360.0f;

    if (openDeg > 0.5f) gfx.fillArc(cx, cy, rInner, rOuter, 0, openDeg, theme::CYAN);
    if (openDeg < 359.5f) gfx.fillArc(cx, cy, rInner, rOuter, openDeg, 360, theme::GREY);
}
}  // namespace

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
    chrome::drawHeader(gfx, "PORT MAPPING");

    bool running = g_portScanManager.isRunning();
    bool relevant = isForThisHost();

    gfx.setTextColor(theme::GREEN, theme::BG);
    gfx.setCursor(6, 18);
    gfx.print("target: ");
    gfx.print(_target.toString());

    if (!relevant && !running) {
        gfx.setTextColor(theme::GREEN, theme::BG);
        gfx.setCursor(6, 30);
        gfx.print("range: ");
        gfx.print(g_config.portRangeStart);
        gfx.print("-");
        gfx.print(g_config.portRangeEnd);

        gfx.setTextColor(theme::MAGENTA, theme::BG);
        gfx.setCursor(6, 46);
        gfx.print("ENTER: start scan");

        gfx.setTextColor(theme::GREY, theme::BG);
        gfx.setCursor(4, gfx.height() - 9);
        gfx.print("DEL:back");
        return;
    }

    size_t count = g_portScanManager.resultCount();

    gfx.setTextColor(running ? theme::CYAN : theme::GREEN, theme::BG);
    gfx.setCursor(4, 28);
    gfx.print(running ? "scanning " : "done ");
    gfx.print(g_portScanManager.progressPct());
    gfx.print("% open:");
    gfx.print((unsigned)count);

    drawResults(gfx, 38);

    drawTopPortsFooter(gfx, count);

    // Donut overlay, top-right corner — drawn last so its background
    // fill also clears whatever table content spilled under it (only
    // the first couple of rows' banner column, at worst).
    {
        constexpr int16_t kPanelX = 184, kPanelY = 17, kPanelW = 52, kPanelH = 46;
        int16_t cx = kPanelX + kPanelW / 2;
        int16_t cy = kPanelY + kPanelH / 2;
        gfx.fillRect(kPanelX, kPanelY, kPanelW, kPanelH, theme::BG);

        uint32_t totalRange = (uint32_t)g_config.portRangeEnd - g_config.portRangeStart + 1;
        float openFrac = totalRange > 0 ? (float)count / (float)totalRange : 0.f;
        // count/totalRange is already shown as text in the stat line
        // above ("open:N") — the donut stays purely visual, no
        // redundant label crammed into this small a panel.
        drawOpenPortsDonut(gfx, cx, cy, 20, 12, openFrac);
    }

    gfx.setTextColor(theme::GREY, theme::BG);
    gfx.setCursor(4, gfx.height() - 9);
    gfx.print(running ? "DEL:back" : "ENTER:rescan  DEL:back");
}

void PortScanScreen::drawResults(M5Canvas& gfx, int16_t top) {
    constexpr int16_t kRowH = 10;
    constexpr int16_t kMaxRows = 7;  // leaves room for the "top ports" summary line above the footer

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
        uint16_t rowBg = sel ? theme::PANEL_BG : theme::BG;
        if (sel) gfx.fillRect(0, y, gfx.width(), kRowH, rowBg);

        bool risky = (r.port == 21 || r.port == 23 || r.port == 139 || r.port == 445 || r.port == 3389);
        uint16_t color = sel ? theme::CYAN : (risky ? theme::AMBER : theme::GREEN);
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

void PortScanScreen::drawTopPortsFooter(M5Canvas& gfx, size_t count) {
    // "TOP OPEN PORTS: 22, 80, 443, ..." — only real, discovered data,
    // truncated (never fabricated) if it would overflow the line.
    String line = "OPEN PORTS: ";
    PortResult r;
    for (size_t i = 0; i < count; i++) {
        if (!g_portScanManager.getResult(i, r)) continue;
        String next = (i == 0) ? String(r.port) : ("," + String(r.port));
        if (line.length() + next.length() > 38) {
            line += "...";
            break;
        }
        line += next;
    }
    if (count == 0) line += "-";

    gfx.setTextColor(theme::GREY, theme::BG);
    gfx.setCursor(4, gfx.height() - 19);
    gfx.print(line);
}
