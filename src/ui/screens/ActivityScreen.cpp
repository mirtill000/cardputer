#include "ActivityScreen.h"
#include "../UiManager.h"
#include "../Theme.h"
#include "../Chrome.h"
#include "../ActivityStatus.h"

ActivityScreen& ActivityScreen::instance() {
    static ActivityScreen s;
    return s;
}

void ActivityScreen::onEnter() {
    _selected = 0;
}

void ActivityScreen::onKey(UiKey key, char /*ch*/) {
    switch (key) {
        case UiKey::Up:
            if (_selected > 0) _selected--;
            break;
        case UiKey::Down: {
            activity::TaskStatus all[32];
            size_t n = activity::list(all, 32);
            if (_selected + 1 < n) _selected++;
            break;
        }
        case UiKey::Back:
            g_ui.popScreen();
            break;
        default:
            break;
    }
}

void ActivityScreen::draw(M5Canvas& gfx) {
    gfx.fillScreen(theme::BG);
    chrome::drawHeader(gfx, "ACTIVITY");

    activity::TaskStatus all[32];
    size_t n = activity::list(all, 32);

    // Running tasks first (stable within each group) — the whole point
    // of this screen is "what's still going that I might have
    // forgotten about", so those belong at the top, not buried in
    // alphabetical/table order among two dozen idle ones.
    size_t order[32];
    size_t oi = 0;
    for (size_t i = 0; i < n; i++)
        if (all[i].running) order[oi++] = i;
    for (size_t i = 0; i < n; i++)
        if (!all[i].running) order[oi++] = i;

    if (oi == 0) {
        chrome::drawEmptyState(gfx, "nothing tracked", "");
        gfx.setTextColor(theme::GREY, theme::BG);
        gfx.setCursor(4, gfx.height() - 9);
        gfx.print("DEL:back");
        return;
    }
    if (_selected >= oi) _selected = oi - 1;

    constexpr int16_t kRowH = 12;
    constexpr int16_t kTop = 18;
    constexpr size_t kMaxRows = 8;

    size_t first = 0;
    if (_selected >= kMaxRows) first = _selected - kMaxRows + 1;

    for (size_t row = 0; row < kMaxRows; row++) {
        size_t idx = first + row;
        if (idx >= oi) break;
        const activity::TaskStatus& t = all[order[idx]];
        int16_t y = kTop + (int16_t)row * kRowH;
        bool sel = (idx == _selected);
        uint16_t rowBg = sel ? theme::PANEL_BG : theme::BG;
        if (sel) gfx.fillRect(0, y - 1, gfx.width(), kRowH, rowBg);

        gfx.fillCircle(8, y + 3, 2, t.running ? theme::GREEN : theme::GREY);

        gfx.setTextColor(sel ? theme::CYAN : (t.running ? theme::GREEN_BRIGHT : theme::GREY), rowBg);
        gfx.setCursor(14, y);
        String label = t.label;
        if (label.length() > 27) label = label.substring(0, 27);
        gfx.print(label);

        gfx.setTextColor(sel ? theme::CYAN : theme::MAGENTA, rowBg);
        gfx.setCursor(gfx.width() - 18, y);
        gfx.print(t.isRf ? "RF" : "BG");
    }

    chrome::drawScrollMarkers(gfx, kTop, kTop + (int16_t)kMaxRows * kRowH, first > 0, (first + kMaxRows) < oi);

    size_t runningCount = 0;
    for (size_t i = 0; i < n; i++)
        if (all[i].running) runningCount++;

    gfx.setTextColor(theme::GREY, theme::BG);
    gfx.setCursor(4, gfx.height() - 19);
    gfx.print((unsigned)runningCount);
    gfx.print("/");
    gfx.print((unsigned)n);
    gfx.print(" running");

    gfx.setCursor(4, gfx.height() - 9);
    gfx.print("DEL:back");
}
