#include "ScanHistoryScreen.h"
#include "../UiManager.h"
#include "../Theme.h"
#include "../Chrome.h"
#include "../../storage/SdCard.h"
#include <cstdio>

ScanHistoryScreen& ScanHistoryScreen::instance() {
    static ScanHistoryScreen s;
    return s;
}

void ScanHistoryScreen::onEnter() {
    _state = State::List;
    _selected = 0;
    ScanHistory::listEntries(sdcard::exportFs(), _entries);
}

void ScanHistoryScreen::openSelected() {
    if (_selected >= _entries.size()) return;
    if (ScanHistory::loadEntry(sdcard::exportFs(), _entries[_selected].filename, _detailHosts)) {
        _detailSelected = 0;
        _state = State::Detail;
    }
}

void ScanHistoryScreen::onKey(UiKey key, char /*ch*/) {
    if (_state == State::List) {
        switch (key) {
            case UiKey::Up:
                if (_selected > 0) _selected--;
                break;
            case UiKey::Down:
                if (_selected + 1 < _entries.size()) _selected++;
                break;
            case UiKey::Enter:
                openSelected();
                break;
            case UiKey::Back:
                g_ui.popScreen();
                break;
            default:
                break;
        }
        return;
    }

    // State::Detail
    switch (key) {
        case UiKey::Up:
            if (_detailSelected > 0) _detailSelected--;
            break;
        case UiKey::Down:
            if (_detailSelected + 1 < _detailHosts.size()) _detailSelected++;
            break;
        case UiKey::Back:
            _state = State::List;
            break;
        default:
            break;
    }
}

void ScanHistoryScreen::draw(M5Canvas& gfx) {
    gfx.fillScreen(theme::BG);

    if (_state == State::List) {
        chrome::drawHeader(gfx, "SCAN HISTORY");
        drawList(gfx, 20);
        gfx.setTextColor(theme::GREY, theme::BG);
        gfx.setCursor(4, gfx.height() - 9);
        gfx.print(_entries.empty() ? "DEL:back" : "ENTER:view  DEL:back");
    } else {
        char title[24];
        snprintf(title, sizeof(title), "SCAN #%05u", (unsigned)_entries[_selected].seq);
        chrome::drawHeader(gfx, title);
        drawDetail(gfx, 20);
        gfx.setTextColor(theme::GREY, theme::BG);
        gfx.setCursor(4, gfx.height() - 9);
        gfx.print("DEL:back");
    }
}

void ScanHistoryScreen::drawList(M5Canvas& gfx, int16_t top) {
    if (_entries.empty()) {
        gfx.setTextColor(theme::AMBER, theme::BG);
        gfx.setCursor(6, top + 4);
        gfx.print("no scans yet - run NETWORK SCAN first");
        return;
    }

    constexpr int16_t kRowH = 10;
    constexpr int16_t kMaxRows = 10;

    size_t first = 0;
    if (_selected >= kMaxRows) first = _selected - kMaxRows + 1;

    for (size_t row = 0; row < kMaxRows; row++) {
        size_t i = first + row;
        if (i >= _entries.size()) break;
        const auto& e = _entries[i];

        int16_t y = top + (int16_t)row * kRowH;
        bool sel = (i == _selected);
        uint16_t rowBg = sel ? theme::PANEL_BG : theme::BG;
        if (sel) gfx.fillRect(0, y, gfx.width(), kRowH, rowBg);

        gfx.setTextColor(sel ? theme::CYAN : theme::GREEN, rowBg);
        gfx.setCursor(4, y + 1);
        gfx.print('#');
        char seqBuf[12];
        snprintf(seqBuf, sizeof(seqBuf), "%05u", (unsigned)e.seq);
        gfx.print(seqBuf);

        gfx.setCursor(90, y + 1);
        gfx.print((unsigned)e.hostCount);
        gfx.print(" hosts");

        if (i == 0) {
            gfx.setTextColor(theme::MAGENTA, rowBg);
            gfx.setCursor(180, y + 1);
            gfx.print("latest");
        }
    }
}

void ScanHistoryScreen::drawDetail(M5Canvas& gfx, int16_t top) {
    if (_detailHosts.empty()) {
        gfx.setTextColor(theme::AMBER, theme::BG);
        gfx.setCursor(6, top + 4);
        gfx.print("no alive hosts in this scan");
        return;
    }

    constexpr int16_t kBoxBottom = 122;
    constexpr int16_t kColIp = 4;
    constexpr int16_t kColClass = 4 + 16 * theme::GLYPH_W;
    constexpr int16_t kColVendor = kColClass + 8 * theme::GLYPH_W;
    constexpr int16_t kVendorMaxChars = 14;

    gfx.drawRect(2, top, gfx.width() - 4, kBoxBottom - top, theme::GREY);

    gfx.setTextColor(theme::MAGENTA, theme::BG);
    gfx.setCursor(kColIp, top + 2);
    gfx.print("IP");
    gfx.setCursor(kColClass, top + 2);
    gfx.print("TYPE");
    gfx.setCursor(kColVendor, top + 2);
    gfx.print("VENDOR");

    constexpr int16_t kHeaderH = 11;
    gfx.drawFastHLine(3, top + kHeaderH, gfx.width() - 6, theme::GREY);

    constexpr int16_t kRowH = 10;
    int16_t rowsTop = top + kHeaderH + 2;
    int16_t kMaxRows = (kBoxBottom - rowsTop) / kRowH;

    size_t first = 0;
    if ((int16_t)_detailSelected >= kMaxRows) first = _detailSelected - (size_t)kMaxRows + 1;

    for (int16_t row = 0; row < kMaxRows; row++) {
        size_t i = first + (size_t)row;
        if (i >= _detailHosts.size()) break;
        const auto& h = _detailHosts[i];

        int16_t y = rowsTop + row * kRowH;
        bool sel = (i == _detailSelected);
        uint16_t rowBg = sel ? theme::PANEL_BG : theme::BG;
        if (sel) gfx.fillRect(3, y, gfx.width() - 6, kRowH, rowBg);

        uint16_t color = sel ? theme::CYAN : theme::GREEN;
        if (h.risk == "critical") color = sel ? theme::CYAN : theme::RED;
        else if (h.risk == "warning") color = sel ? theme::CYAN : theme::AMBER;

        gfx.setTextColor(color, rowBg);
        gfx.setCursor(kColIp, y + 1);
        gfx.print(h.ip.toString());

        gfx.setCursor(kColClass, y + 1);
        gfx.print(h.deviceClass);

        String vendor = h.vendor.length() ? h.vendor : "-";
        if (vendor.length() > kVendorMaxChars) vendor = vendor.substring(0, kVendorMaxChars);
        gfx.setCursor(kColVendor, y + 1);
        gfx.print(vendor);
    }
}
