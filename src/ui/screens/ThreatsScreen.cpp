#include "ThreatsScreen.h"
#include "../UiManager.h"
#include "../Theme.h"
#include "../Chrome.h"
#include "../../core/Finding.h"
#include "../../scan/FindingStore.h"
#include <vector>

ThreatsScreen& ThreatsScreen::instance() {
    static ThreatsScreen s;
    return s;
}

void ThreatsScreen::onEnter() {
    _selected = 0;
}

void ThreatsScreen::onKey(UiKey key, char ch) {
    if (_showDetail) {
        _showDetail = false;
        return;
    }
    if (key == UiKey::Up) {
        if (_selected > 0) _selected--;
    } else if (key == UiKey::Down) {
        _selected++;  // clamped against the live count in draw()
    } else if (key == UiKey::Char && (ch == 'i' || ch == 'I')) {
        _showDetail = true;  // draw() no-ops it if there's nothing at _selected
    } else if (key == UiKey::Back) {
        g_ui.popScreen();
    }
}

namespace {
// A finding's row/overlay color follows its severity, one mapping shared
// by every screen that shows findings. Ok is the "informational status
// note" tier (e.g. a completed PMKID sweep) — cyan, distinct from the
// green "nothing flagged" empty state.
uint16_t severityColor(RiskLevel sev) {
    switch (sev) {
        case RiskLevel::Critical: return theme::RED;
        case RiskLevel::Warning:  return theme::AMBER;
        case RiskLevel::Ok:       return theme::CYAN;
    }
    return theme::GREY;
}
}  // namespace

void ThreatsScreen::draw(M5Canvas& gfx) {
    // Single source of truth: the same rollup the HTML report and the
    // JSON export emit (see scan/FindingStore.h). THREATS used to
    // re-derive this itself from six managers; that logic now lives in
    // FindingStore so all three finally agree.
    std::vector<Finding> findings;
    g_findings.buildRollup(findings);

    if (_selected >= findings.size()) _selected = findings.empty() ? 0 : findings.size() - 1;

    if (_showDetail) {
        if (_selected < findings.size()) {
            const Finding& f = findings[_selected];
            String body = String(findingCategoryLabel(f.category)) + " / " +
                          (f.severity == RiskLevel::Critical ? "CRITICAL"
                           : f.severity == RiskLevel::Warning ? "WARNING"
                                                              : "INFO") +
                          "\n\n" + (f.detail.length() ? f.detail : f.title);
            chrome::drawDetailOverlay(gfx, "THREAT FINDING", body);
        }
        return;
    }

    gfx.fillScreen(theme::BG);
    chrome::drawHeader(gfx, "THREATS");

    gfx.setTextColor(findings.empty() ? theme::GREEN : theme::RED, theme::BG);
    gfx.setCursor(6, 18);
    gfx.print("findings: ");
    gfx.print((unsigned)findings.size());

    if (findings.empty()) {
        gfx.setTextColor(theme::GREY, theme::BG);
        gfx.setCursor(6, 40);
        gfx.print("nothing flagged (yet).");
        gfx.setCursor(6, 52);
        gfx.print("run NETWORK SCAN / audits to");
        gfx.setCursor(6, 64);
        gfx.print("populate this view.");
    } else {
        int16_t top = 28;
        gfx.setTextColor(theme::GREY, theme::BG);
        gfx.drawFastHLine(4, top, gfx.width() - 8, theme::GREY);

        constexpr int16_t kRowH = 11;
        constexpr size_t kMaxRows = 8;
        size_t first = 0;
        if (_selected >= kMaxRows) first = _selected - kMaxRows + 1;

        for (size_t row = 0; row < kMaxRows; row++) {
            size_t i = first + row;
            if (i >= findings.size()) break;
            int16_t y = top + 3 + (int16_t)row * kRowH;
            bool sel = (i == _selected);
            uint16_t rowBg = sel ? theme::PANEL_BG : theme::BG;
            if (sel) gfx.fillRect(0, y - 1, gfx.width(), kRowH, rowBg);
            gfx.setTextColor(sel ? theme::CYAN : severityColor(findings[i].severity), rowBg);
            gfx.setCursor(6, y);
            String t = findings[i].title;
            if (t.length() > 38) t = t.substring(0, 38);
            gfx.print(t);
        }

        chrome::drawScrollMarkers(gfx, top + 3, top + 3 + (int16_t)kMaxRows * kRowH, first > 0,
                                   (first + kMaxRows) < findings.size());
    }

    chrome::drawFooter(gfx, findings.empty() ? "DEL:back" : "I:full text  DEL:back");
}
