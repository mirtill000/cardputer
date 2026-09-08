#pragma once

#include "Screen.h"
#include "../Chrome.h"
#include "../Theme.h"
#include "../UiManager.h"
#include <M5GFX.h>

// Shared base for the ~30 screens in this app that are, structurally, the
// same thing: a scrolling, single-selection list with a header, an
// optional status line, a windowed body (the "first / kMaxRows"
// convention every one of them already uses), the shared ^/v scroll
// markers, an optional 'I'-toggled full-value overlay, and the shared
// footer. Every one of those screens re-implements that skeleton by hand
// today — the selection clamp, the Up/Down/Back keys, the windowing math,
// the scroll-marker call — ~40 near-identical lines apiece. This base
// owns the skeleton so a list screen only has to say what its rows ARE,
// not re-derive how a list behaves; that's what makes them feel like one
// app's lists instead of thirty independent ones.
//
// A subclass provides the required hooks; the optional ones default to
// the most common behavior. Navigation keys (Up/Down/Back/I) are handled
// here; anything else is handed to onListKey() so a screen can add its
// own (ENTER to start/stop, X to delete, TAB to switch view, ...).
//
// NOTE: this is ready infrastructure with the interface modeled on the
// existing screens; migrating each screen onto it is a mechanical,
// screen-by-screen change best done with a hardware build to eyeball
// each layout, so it is intentionally rolled out incrementally rather
// than as one blind sweep. New list screens should derive from it.
class ListScreen : public Screen {
public:
    void onKey(UiKey key, char ch) override {
        if (_showDetail) {  // any key closes the overlay
            _showDetail = false;
            return;
        }
        switch (key) {
            case UiKey::Up:
                if (_selected > 0) _selected--;
                return;
            case UiKey::Down:
                _selected++;  // clamped against the live count in draw()
                return;
            case UiKey::Back:
                g_ui.popScreen();
                return;
            case UiKey::Char:
                if ((ch == 'i' || ch == 'I') && detailText(_selected).length()) {
                    _showDetail = true;
                    return;
                }
                break;  // fall through to the screen's own keys
            default:
                break;
        }
        onListKey(key, ch);
    }

    void draw(M5Canvas& gfx) override {
        size_t count = itemCount();
        if (_selected >= count) _selected = count ? count - 1 : 0;

        if (_showDetail && _selected < count) {
            String d = detailText(_selected);
            if (d.length()) {
                chrome::drawDetailOverlay(gfx, detailTitle(), d);
                return;
            }
            _showDetail = false;  // nothing to show; fall back to the list
        }

        gfx.fillScreen(theme::BG);
        chrome::drawHeader(gfx, listHeader());
        drawAboveList(gfx);

        if (count == 0) {
            drawEmpty(gfx);
            chrome::drawFooter(gfx, listFooter());
            return;
        }

        const int16_t top = listTop();
        const int16_t rowH = rowHeight();
        const size_t rows = maxRows();
        size_t first = 0;
        if (_selected >= rows) first = _selected - rows + 1;

        for (size_t r = 0; r < rows; r++) {
            size_t i = first + r;
            if (i >= count) break;
            int16_t y = top + (int16_t)r * rowH;
            drawItem(gfx, i, y, rowH, i == _selected);
        }

        chrome::drawScrollMarkers(gfx, top, top + (int16_t)rows * rowH, first > 0, (first + rows) < count);
        chrome::drawFooter(gfx, listFooter());
    }

protected:
    // --- required ---
    virtual size_t itemCount() const = 0;
    virtual void drawItem(M5Canvas& gfx, size_t index, int16_t y, int16_t rowH, bool selected) = 0;
    virtual const char* listHeader() const = 0;
    virtual const char* listFooter() const = 0;

    // --- optional, sensible defaults matching the common screens ---
    virtual int16_t listTop() const { return 30; }
    virtual int16_t rowHeight() const { return 10; }
    virtual size_t maxRows() const { return 7; }
    virtual void drawAboveList(M5Canvas& gfx) { (void)gfx; }  // status line between header and list
    virtual void drawEmpty(M5Canvas& gfx) { (void)gfx; }      // shown when itemCount()==0
    virtual String detailText(size_t index) const { (void)index; return String(); }  // "" => no 'I' overlay
    virtual const char* detailTitle() const { return "DETAIL"; }
    virtual void onListKey(UiKey key, char ch) { (void)key; (void)ch; }  // extra keys

    size_t selected() const { return _selected; }
    void setSelected(size_t s) { _selected = s; }
    void resetSelection() { _selected = 0; }

private:
    size_t _selected = 0;
    bool _showDetail = false;
};
