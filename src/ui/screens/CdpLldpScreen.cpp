#include "CdpLldpScreen.h"
#include "../UiManager.h"
#include "../Theme.h"
#include "../Chrome.h"
#include "../../scan/CdpLldpSniffer.h"

CdpLldpScreen& CdpLldpScreen::instance() {
    static CdpLldpScreen s;
    return s;
}

void CdpLldpScreen::onEnter() {
    _running = g_cdpLldpSniffer.isRunning();
    _logCount = 0;
}

void CdpLldpScreen::pushLog(const String& line) {
    if (_logCount < kLogLines) {
        _log[_logCount++] = line;
    } else {
        for (uint8_t i = 1; i < kLogLines; i++) _log[i - 1] = _log[i];
        _log[kLogLines - 1] = line;
    }
}

void CdpLldpScreen::onScanEvent(const ScanNotification& ev) {
    if (ev.source != ScanSource::CdpLldp) return;
    if (ev.type == ScanEventType::LogLine) pushLog(String(ev.text));
}

void CdpLldpScreen::onKey(UiKey key, char ch) {
    if (_showDetail) {
        _showDetail = false;
        return;
    }
    if (key == UiKey::Enter) {
        if (_running) {
            g_cdpLldpSniffer.stop();
        } else {
            g_cdpLldpSniffer.start();
        }
        _running = !_running;
    } else if (key == UiKey::Up) {
        if (_selected > 0) _selected--;
    } else if (key == UiKey::Down) {
        if (_selected + 1 < g_cdpLldpSniffer.neighborCount()) _selected++;
    } else if (key == UiKey::Char && (ch == 'i' || ch == 'I')) {
        if (g_cdpLldpSniffer.neighborCount() > 0) _showDetail = true;
    } else if (key == UiKey::Back) {
        g_ui.popScreen();  // keeps running in the background - see CdpLldpSniffer
    }
}

void CdpLldpScreen::draw(M5Canvas& gfx) {
    if (_showDetail) {
        CdpLldpSniffer::Neighbor n;
        if (g_cdpLldpSniffer.getNeighbor(_selected, n)) {
            String text = "deviceId: " + n.deviceId + " / portId: " + (n.portId.length() ? n.portId : String("-"));
            chrome::drawDetailOverlay(gfx, n.isCdp ? "CDP NEIGHBOR" : "LLDP NEIGHBOR", text);
        }
        return;
    }

    gfx.fillScreen(theme::BG);
    chrome::drawHeader(gfx, "LAN TOPOLOGY");

    gfx.setTextColor(_running ? theme::CYAN : theme::GREEN, theme::BG);
    gfx.setCursor(6, 18);
    gfx.print("neighbors: ");
    gfx.print((unsigned)g_cdpLldpSniffer.neighborCount());
    gfx.print(_running ? "  [listening]" : "");

    drawNeighbors(gfx, 30);

    gfx.setTextColor(theme::MAGENTA, theme::BG);
    gfx.setCursor(6, gfx.height() - 20);
    gfx.print(_running ? "ENTER: stop" : "ENTER: start passive listen");

    gfx.setTextColor(theme::GREY, theme::BG);
    gfx.setCursor(4, gfx.height() - 9);
    gfx.print(g_cdpLldpSniffer.neighborCount() > 0 ? "I:full name  DEL:back" : "DEL:back");
}

void CdpLldpScreen::drawNeighbors(M5Canvas& gfx, int16_t top) {
    size_t count = g_cdpLldpSniffer.neighborCount();
    if (count == 0) return;

    gfx.setTextColor(theme::GREY, theme::BG);
    gfx.drawFastHLine(4, top, gfx.width() - 8, theme::GREY);

    constexpr int16_t kRowH = 10;
    constexpr size_t kMaxRows = 9;

    size_t first = 0;
    if (_selected >= kMaxRows) first = _selected - kMaxRows + 1;

    CdpLldpSniffer::Neighbor n;
    for (size_t row = 0; row < kMaxRows; row++) {
        size_t i = first + row;
        if (i >= count) break;
        if (!g_cdpLldpSniffer.getNeighbor(i, n)) continue;

        int16_t y = top + 2 + (int16_t)row * kRowH;
        bool sel = (i == _selected);
        uint16_t rowBg = sel ? theme::PANEL_BG : theme::BG;
        if (sel) gfx.fillRect(0, y, gfx.width(), kRowH, rowBg);

        gfx.setTextColor(sel ? theme::CYAN : (n.isCdp ? theme::AMBER : theme::GREEN), rowBg);
        gfx.setCursor(6, y);
        String label = n.portId.length() ? (n.deviceId + " (" + n.portId + ")") : n.deviceId;
        if (label.length() > 25) label = label.substring(0, 25);
        gfx.print(label);

        gfx.setCursor(210, y);
        gfx.print(n.isCdp ? "C" : "L");
    }

    chrome::drawScrollMarkers(gfx, top + 2, top + 2 + (int16_t)kMaxRows * kRowH, first > 0, (first + kMaxRows) < count);
}
