#include "NtlmHttpScreen.h"
#include "../UiManager.h"
#include "../Theme.h"
#include "../Chrome.h"
#include "../../scan/NtlmHttpProbe.h"

NtlmHttpScreen& NtlmHttpScreen::instance() {
    static NtlmHttpScreen s;
    return s;
}

void NtlmHttpScreen::onEnter() {
    _selected = 0;
}

void NtlmHttpScreen::onScanEvent(const ScanNotification& ev) {
    (void)ev;  // list pulled live from the probe each draw
}

void NtlmHttpScreen::onKey(UiKey key, char ch) {
    if (_showDetail) {
        _showDetail = false;
        return;
    }
    if (key == UiKey::Enter) {
        if (!g_ntlmHttpProbe.isRunning()) g_ntlmHttpProbe.start();
    } else if (key == UiKey::Up) {
        if (_selected > 0) _selected--;
    } else if (key == UiKey::Down) {
        if (_selected + 1 < g_ntlmHttpProbe.count()) _selected++;
    } else if (key == UiKey::Char && (ch == 'i' || ch == 'I')) {
        if (g_ntlmHttpProbe.count() > 0) _showDetail = true;
    } else if (key == UiKey::Back) {
        g_ui.popScreen();
    }
}

void NtlmHttpScreen::draw(M5Canvas& gfx) {
    if (_showDetail) {
        NtlmHttpProbe::Finding f;
        if (g_ntlmHttpProbe.get(_selected, f)) {
            String text = "netbiosDomain: " + (f.netbiosDomain.length() ? f.netbiosDomain : String("-")) +
                          " / netbiosComputer: " + (f.netbiosComputer.length() ? f.netbiosComputer : String("-")) +
                          " / dnsDomain: " + (f.dnsDomain.length() ? f.dnsDomain : String("-")) +
                          " / dnsComputer: " + (f.dnsComputer.length() ? f.dnsComputer : String("-"));
            String title = f.ip.toString() + ":" + String(f.port);
            chrome::drawDetailOverlay(gfx, title.c_str(), text);
        }
        return;
    }

    gfx.fillScreen(theme::BG);
    chrome::drawHeader(gfx, "NTLM DISCLOSURE");

    bool running = g_ntlmHttpProbe.isRunning();
    gfx.setTextColor(running ? theme::CYAN : theme::GREEN, theme::BG);
    gfx.setCursor(6, 18);
    gfx.print("responders: ");
    gfx.print((unsigned)g_ntlmHttpProbe.count());
    if (running) {
        gfx.print("  ");
        gfx.print((unsigned)g_ntlmHttpProbe.progressPct());
        gfx.print("%");
    }

    drawFindings(gfx, 30);

    if (g_ntlmHttpProbe.count() == 0 && !running) {
        chrome::drawEmptyState(gfx, "no NTLM disclosure found", "ENTER: sweep HTTP hosts (needs a scan)");
    }

    gfx.setTextColor(theme::MAGENTA, theme::BG);
    gfx.setCursor(6, gfx.height() - 20);
    gfx.print(running ? "negotiating NTLM..." : "ENTER: sweep known HTTP ports");

    gfx.setTextColor(theme::GREY, theme::BG);
    gfx.setCursor(4, gfx.height() - 9);
    gfx.print(g_ntlmHttpProbe.count() > 0 ? "I:full detail  DEL:back" : "DEL:back  (needs PORT SCAN first)");
}

void NtlmHttpScreen::drawFindings(M5Canvas& gfx, int16_t top) {
    size_t count = g_ntlmHttpProbe.count();
    if (count == 0) return;

    gfx.setTextColor(theme::GREY, theme::BG);
    gfx.drawFastHLine(4, top, gfx.width() - 8, theme::GREY);

    constexpr int16_t kRowH = 10;
    constexpr size_t kMaxRows = 4;  // two lines per finding

    size_t first = 0;
    if (_selected >= kMaxRows) first = _selected - kMaxRows + 1;

    NtlmHttpProbe::Finding f;
    for (size_t row = 0; row < kMaxRows; row++) {
        size_t i = first + row;
        if (i >= count) break;
        if (!g_ntlmHttpProbe.get(i, f)) continue;

        int16_t y = top + 2 + (int16_t)row * (kRowH * 2);
        bool sel = (i == _selected);
        uint16_t rowBg = sel ? theme::PANEL_BG : theme::BG;
        if (sel) gfx.fillRect(0, y, gfx.width(), kRowH * 2 - 2, rowBg);

        gfx.setTextColor(sel ? theme::CYAN : theme::AMBER, rowBg);
        gfx.setCursor(6, y);
        gfx.print(f.ip.toString());
        gfx.setCursor(120, y);
        gfx.print(f.port);

        gfx.setTextColor(theme::GREY, rowBg);
        gfx.setCursor(6, y + kRowH);
        String detail = f.dnsDomain.length() ? f.dnsDomain : f.netbiosDomain;
        if (f.dnsComputer.length()) detail += " / " + f.dnsComputer;
        else if (f.netbiosComputer.length())
            detail += " / " + f.netbiosComputer;
        if (!detail.length()) detail = "NTLM offered, no domain disclosed";
        if (detail.length() > 38) detail = detail.substring(0, 38);
        gfx.print(detail);
    }

    chrome::drawScrollMarkers(gfx, top + 2, top + 2 + (int16_t)kMaxRows * (kRowH * 2), first > 0,
                               (first + kMaxRows) < count);
}
