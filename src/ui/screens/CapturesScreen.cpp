#include "CapturesScreen.h"
#include "../UiManager.h"
#include "../Theme.h"
#include "../Chrome.h"
#include "../../storage/SdCard.h"
#include <FS.h>

CapturesScreen& CapturesScreen::instance() {
    static CapturesScreen s;
    return s;
}

void CapturesScreen::onEnter() {
    _selected = 0;
    _confirmDelete = false;
    _showDetail = false;
    rebuild();
}

void CapturesScreen::scanDir(const char* dir) {
    fs::FS& fs = sdcard::exportFs();
    File d = fs.open(dir);
    if (!d || !d.isDirectory()) return;
    File f = d.openNextFile();
    while (f && _entries.size() < 200) {
        if (!f.isDirectory()) {
            String nm = f.name();
            int slash = nm.lastIndexOf('/');
            String base = (slash >= 0) ? nm.substring(slash + 1) : nm;
            if (base.endsWith(".pcap")) {
                Entry e;
                e.path = String(dir) + "/" + base;
                e.name = base;
                e.size = f.size();
                _entries.push_back(e);
            }
        }
        f = d.openNextFile();
    }
}

void CapturesScreen::rebuild() {
    _entries.clear();
    scanDir("/handshakes");
    scanDir("/netrunner");
    if (_selected >= _entries.size()) _selected = _entries.empty() ? 0 : _entries.size() - 1;
}

void CapturesScreen::onKey(UiKey key, char ch) {
    if (_showDetail) {
        _showDetail = false;
        return;
    }
    if (_confirmDelete) {
        if (key == UiKey::Char && (ch == 'y' || ch == 'Y')) {
            if (_selected < _entries.size()) {
                sdcard::exportFs().remove(_entries[_selected].path);
                rebuild();
            }
        }
        _confirmDelete = false;
        return;
    }

    switch (key) {
        case UiKey::Up:
            if (_selected > 0) _selected--;
            break;
        case UiKey::Down:
            if (_selected + 1 < _entries.size()) _selected++;
            break;
        case UiKey::Char:
            if ((ch == 'i' || ch == 'I') && !_entries.empty()) {
                _showDetail = true;
            } else if ((ch == 'x' || ch == 'X') && !_entries.empty()) {
                _confirmDelete = true;
            }
            break;
        case UiKey::Back:
            g_ui.popScreen();
            break;
        default:
            break;
    }
}

void CapturesScreen::draw(M5Canvas& gfx) {
    if (_showDetail && _selected < _entries.size()) {
        const Entry& e = _entries[_selected];
        String text = "path: " + e.path + " / size: " + String(e.size) + " bytes";
        chrome::drawDetailOverlay(gfx, "CAPTURE", text);
        return;
    }

    gfx.fillScreen(theme::BG);
    // Label the header with the filesystem the list is actually reading
    // from (SD when a card is mounted, otherwise on-board flash) - the
    // same exportFs() the rows come from - so it's clear where captures
    // live and get deleted.
    String header = String("CAPTURES (") + sdcard::exportFsLabel() + ")";
    chrome::drawHeader(gfx, header.c_str());

    if (_confirmDelete && _selected < _entries.size()) {
        gfx.setTextColor(theme::RED, theme::BG);
        gfx.setCursor(6, 40);
        gfx.print("Delete ");
        gfx.print(_entries[_selected].name);
        gfx.print("?");
        gfx.setTextColor(theme::MAGENTA, theme::BG);
        gfx.setCursor(6, 54);
        gfx.print("Y: delete   any other: cancel");
        return;
    }

    if (_entries.empty()) {
        chrome::drawEmptyState(gfx, "no captures yet", "PMKID/DEAUTH/SENTINEL write here");
    } else {
        constexpr int16_t kRowH = 10;
        constexpr size_t kMaxRows = 9;
        size_t first = 0;
        if (_selected >= kMaxRows) first = _selected - kMaxRows + 1;
        for (size_t row = 0; row < kMaxRows; row++) {
            size_t i = first + row;
            if (i >= _entries.size()) break;
            const Entry& e = _entries[i];
            int16_t y = 18 + (int16_t)row * kRowH;
            bool sel = (i == _selected);
            uint16_t rowBg = sel ? theme::PANEL_BG : theme::BG;
            if (sel) gfx.fillRect(0, y - 1, gfx.width(), kRowH, rowBg);

            gfx.setTextColor(sel ? theme::CYAN : theme::GREEN, rowBg);
            gfx.setCursor(6, y);
            String nm = e.name;
            if (nm.length() > 28) nm = nm.substring(0, 28);
            gfx.print(nm);

            gfx.setTextColor(sel ? theme::CYAN : theme::GREY, rowBg);
            String sz = (e.size >= 1024) ? (String(e.size / 1024) + "K") : (String(e.size) + "B");
            gfx.setCursor(gfx.width() - (int16_t)sz.length() * theme::GLYPH_W - 6, y);
            gfx.print(sz);
        }
        chrome::drawScrollMarkers(gfx, 18, 18 + (int16_t)kMaxRows * kRowH, first > 0,
                                   (first + kMaxRows) < _entries.size());
    }

    gfx.setTextColor(theme::GREY, theme::BG);
    gfx.setCursor(4, gfx.height() - 9);
    gfx.print(_entries.empty() ? "DEL:back" : "I:detail X:del DEL:back");
}
