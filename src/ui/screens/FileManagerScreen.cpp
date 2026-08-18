#include "FileManagerScreen.h"
#include "../UiManager.h"
#include "../Theme.h"
#include "../Chrome.h"
#include "../../storage/SdCard.h"
#include <FS.h>

FileManagerScreen& FileManagerScreen::instance() {
    static FileManagerScreen s;
    return s;
}

void FileManagerScreen::onEnter() {
    _path = "/";
    _selected = 0;
    _confirmDelete = false;
    rebuild();
}

String FileManagerScreen::fullPath(const String& base) const {
    if (_path == "/") return "/" + base;
    return _path + "/" + base;
}

void FileManagerScreen::jumpTo(const String& path) {
    _path = path;
    _selected = 0;
    rebuild();
}

void FileManagerScreen::rebuild() {
    _entries.clear();
    fs::FS& fs = sdcard::exportFs();
    File dir = fs.open(_path);
    if (dir && dir.isDirectory()) {
        File f = dir.openNextFile();
        while (f && _entries.size() < 60) {
            String nm = f.name();
            int slash = nm.lastIndexOf('/');
            Entry e;
            e.name = (slash >= 0) ? nm.substring(slash + 1) : nm;  // normalize to basename
            e.size = f.size();
            e.dir = f.isDirectory();
            if (e.name.length()) _entries.push_back(e);
            f = dir.openNextFile();
        }
    }
    if (_selected >= _entries.size()) _selected = _entries.empty() ? 0 : _entries.size() - 1;
}

void FileManagerScreen::onKey(UiKey key, char ch) {
    if (_confirmDelete) {
        if (key == UiKey::Char && (ch == 'y' || ch == 'Y')) {
            if (_selected < _entries.size() && !_entries[_selected].dir) {
                sdcard::exportFs().remove(fullPath(_entries[_selected].name));
                rebuild();
            }
        }
        _confirmDelete = false;
        return;
    }

    if (key == UiKey::Up) {
        if (_selected > 0) _selected--;
    } else if (key == UiKey::Down) {
        if (_selected + 1 < _entries.size()) _selected++;
    } else if (key == UiKey::Enter) {
        if (_selected < _entries.size() && _entries[_selected].dir) {
            _path = fullPath(_entries[_selected].name);
            _selected = 0;
            rebuild();
        }
    } else if (key == UiKey::Char && (ch == 'x' || ch == 'X')) {
        if (_selected < _entries.size() && !_entries[_selected].dir) _confirmDelete = true;
    } else if (key == UiKey::Char && (ch == 'n' || ch == 'N')) {
        jumpTo("/netrunner");
    } else if (key == UiKey::Char && (ch == 'h' || ch == 'H')) {
        jumpTo("/handshakes");
    } else if (key == UiKey::Back) {
        if (_path == "/") {
            g_ui.popScreen();
        } else {
            int slash = _path.lastIndexOf('/');
            _path = (slash > 0) ? _path.substring(0, slash) : "/";
            _selected = 0;
            rebuild();
        }
    }
}

void FileManagerScreen::draw(M5Canvas& gfx) {
    gfx.fillScreen(theme::BG);
    chrome::drawHeader(gfx, "FILES");

    gfx.setTextColor(theme::CYAN, theme::BG);
    gfx.setCursor(6, 18);
    String p = _path;
    if (p.length() > 30) p = "..." + p.substring(p.length() - 27);
    gfx.print(p);

    if (_confirmDelete && _selected < _entries.size()) {
        gfx.setTextColor(theme::RED, theme::BG);
        gfx.setCursor(6, 50);
        gfx.print("Delete ");
        gfx.print(_entries[_selected].name);
        gfx.print("?");
        gfx.setTextColor(theme::MAGENTA, theme::BG);
        gfx.setCursor(6, 64);
        gfx.print("Y: delete   any other: cancel");
        return;
    }

    if (_entries.empty()) {
        chrome::drawEmptyState(gfx, "empty directory", "DEL: back");
    } else {
        constexpr int16_t kRowH = 10;
        constexpr size_t kMaxRows = 8;
        size_t first = 0;
        if (_selected >= kMaxRows) first = _selected - kMaxRows + 1;
        for (size_t row = 0; row < kMaxRows; row++) {
            size_t i = first + row;
            if (i >= _entries.size()) break;
            const Entry& e = _entries[i];
            int16_t y = 30 + (int16_t)row * kRowH;
            bool sel = (i == _selected);
            uint16_t rowBg = sel ? theme::PANEL_BG : theme::BG;
            if (sel) gfx.fillRect(0, y - 1, gfx.width(), kRowH, rowBg);
            gfx.setTextColor(sel ? theme::CYAN : (e.dir ? theme::MAGENTA : theme::GREEN), rowBg);
            gfx.setCursor(6, y);
            String nm = e.name;
            if (nm.length() > 26) nm = nm.substring(0, 26);
            gfx.print(e.dir ? "/" : " ");
            gfx.print(nm);
            if (!e.dir) {
                gfx.setTextColor(sel ? theme::CYAN : theme::GREY, rowBg);
                String sz = (e.size >= 1024) ? (String(e.size / 1024) + "K") : (String(e.size) + "B");
                gfx.setCursor(gfx.width() - (int16_t)sz.length() * theme::GLYPH_W - 6, y);
                gfx.print(sz);
            }
        }

        chrome::drawScrollMarkers(gfx, 30, 30 + (int16_t)kMaxRows * kRowH, first > 0,
                                   (first + kMaxRows) < _entries.size());
    }

    gfx.setTextColor(theme::GREY, theme::BG);
    gfx.setCursor(4, gfx.height() - 9);
    gfx.print("ENTER:opendir X:del N/H:jump DEL:up");
}
