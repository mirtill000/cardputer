#include "SearchScreen.h"
#include "HostDetailScreen.h"
#include "../UiManager.h"
#include "../Theme.h"
#include "../Chrome.h"
#include "../../core/Types.h"
#include "../../scan/ScanManager.h"

SearchScreen& SearchScreen::instance() {
    static SearchScreen s;
    return s;
}

void SearchScreen::onEnter() {
    _query = "";
    _results.clear();
    _selected = 0;
    _browsing = false;
    g_ui.setTextEntryMode(true);
    rebuild();
}

void SearchScreen::onExit() {
    g_ui.setTextEntryMode(false);
}

void SearchScreen::rebuild() {
    _results.clear();
    String q = _query;
    q.toLowerCase();
    size_t n = g_scanManager.hostCount();
    HostInfo h;
    for (size_t i = 0; i < n; i++) {
        if (!g_scanManager.getHost(i, h) || !h.alive) continue;
        String hay = h.ip.toString() + " " + (h.macKnown ? macToString(h.mac) : String("")) + " " + h.vendor +
                     " " + h.hostname;
        hay.toLowerCase();
        if (q.length() == 0 || hay.indexOf(q) >= 0) _results.push_back(i);
    }
    if (_selected >= _results.size()) _selected = _results.empty() ? 0 : _results.size() - 1;
}

void SearchScreen::onKey(UiKey key, char ch) {
    if (!_browsing) {
        // Typing the query (text-entry mode: arrows arrive as Char).
        if (key == UiKey::Char) {
            _query += ch;
            rebuild();
        } else if (key == UiKey::Enter) {
            if (!_results.empty()) {
                _browsing = true;
                g_ui.setTextEntryMode(false);
            }
        } else if (key == UiKey::Back) {
            if (_query.length() > 0) {
                _query.remove(_query.length() - 1);
                rebuild();
            } else {
                g_ui.popScreen();
            }
        }
        return;
    }

    // Browsing the matches.
    if (key == UiKey::Up) {
        if (_selected > 0) _selected--;
    } else if (key == UiKey::Down) {
        if (_selected + 1 < _results.size()) _selected++;
    } else if (key == UiKey::Enter) {
        if (_selected < _results.size()) {
            HostDetailScreen::instance().setHostIndex(_results[_selected]);
            g_ui.pushScreen(&HostDetailScreen::instance());
        }
    } else if (key == UiKey::Back) {
        _browsing = false;
        g_ui.setTextEntryMode(true);
    }
}

void SearchScreen::draw(M5Canvas& gfx) {
    gfx.fillScreen(theme::BG);
    chrome::drawHeader(gfx, "SEARCH");

    // Query field.
    gfx.setTextColor(theme::GREY, theme::BG);
    gfx.setCursor(6, 18);
    gfx.print("find: ");
    gfx.fillRect(40, 17, gfx.width() - 46, 10, theme::PANEL_BG);
    gfx.setTextColor(theme::GREEN_BRIGHT, theme::PANEL_BG);
    gfx.setCursor(42, 18);
    gfx.print(_query);
    if (!_browsing && (millis() / 500) % 2 == 0) {
        gfx.setCursor(42 + (int16_t)_query.length() * theme::GLYPH_W, 18);
        gfx.print("_");
    }

    gfx.setTextColor(theme::CYAN, theme::BG);
    gfx.setCursor(6, 29);
    gfx.print((unsigned)_results.size());
    gfx.print(_browsing ? " matches [browse]" : " matches [typing]");

    constexpr int16_t kRowH = 10;
    constexpr size_t kMaxRows = 7;
    size_t first = 0;
    if (_selected >= kMaxRows) first = _selected - kMaxRows + 1;

    HostInfo h;
    for (size_t row = 0; row < kMaxRows; row++) {
        size_t i = first + row;
        if (i >= _results.size()) break;
        if (!g_scanManager.getHost(_results[i], h)) continue;
        int16_t y = 40 + (int16_t)row * kRowH;
        bool sel = (_browsing && i == _selected);
        uint16_t rowBg = sel ? theme::PANEL_BG : theme::BG;
        if (sel) gfx.fillRect(0, y - 1, gfx.width(), kRowH, rowBg);
        gfx.setTextColor(sel ? theme::CYAN : theme::GREEN, rowBg);
        gfx.setCursor(6, y);
        gfx.print(h.ip.toString());
        gfx.setTextColor(sel ? theme::CYAN : theme::GREY, rowBg);
        gfx.setCursor(110, y);
        String label = h.hostname.length() ? h.hostname : (h.vendor.length() ? h.vendor : String("-"));
        if (label.length() > 21) label = label.substring(0, 21);
        gfx.print(label);
    }

    chrome::drawScrollMarkers(gfx, 40, 40 + (int16_t)kMaxRows * kRowH, first > 0, (first + kMaxRows) < _results.size());

    gfx.setTextColor(theme::GREY, theme::BG);
    gfx.setCursor(4, gfx.height() - 9);
    gfx.print(_browsing ? "ENTER:open DEL:edit ?:help" : "type ENTER:browse DEL:back");
}
