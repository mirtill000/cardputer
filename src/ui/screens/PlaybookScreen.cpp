#include "PlaybookScreen.h"
#include "../UiManager.h"
#include "../Theme.h"
#include "../Chrome.h"
#include "../../scan/PlaybookRunner.h"

PlaybookScreen& PlaybookScreen::instance() {
    static PlaybookScreen s;
    return s;
}

void PlaybookScreen::onEnter() {
    if (_selected >= PlaybookRunner::playbookCount()) _selected = 0;
    _logCount = 0;
}

void PlaybookScreen::pushLog(const String& line) {
    if (_logCount < kLogLines) {
        _log[_logCount++] = line;
    } else {
        for (uint8_t i = 1; i < kLogLines; i++) _log[i - 1] = _log[i];
        _log[kLogLines - 1] = line;
    }
}

void PlaybookScreen::onScanEvent(const ScanNotification& ev) {
    if (ev.source != ScanSource::Playbook) return;
    if (ev.type == ScanEventType::LogLine) pushLog(String(ev.text));
    else if (ev.type == ScanEventType::ScanStarted) _logCount = 0;
}

void PlaybookScreen::onKey(UiKey key, char /*ch*/) {
    bool running = g_playbookRunner.isRunning();
    if (running) {
        if (key == UiKey::Enter) {
            g_playbookRunner.stop();
        } else if (key == UiKey::Back) {
            g_ui.popScreen();  // keeps running in the background
        }
        return;
    }

    size_t count = PlaybookRunner::playbookCount();
    if (key == UiKey::Up) {
        if (_selected > 0) _selected--;
    } else if (key == UiKey::Down) {
        if (_selected + 1 < count) _selected++;
    } else if (key == UiKey::Enter) {
        if (count > 0) {
            g_playbookRunner.start(_selected);
            _logCount = 0;
        }
    } else if (key == UiKey::Back) {
        g_ui.popScreen();
    }
}

void PlaybookScreen::draw(M5Canvas& gfx) {
    gfx.fillScreen(theme::BG);
    chrome::drawHeader(gfx, "PLAYBOOK");

    if (g_playbookRunner.isRunning()) {
        drawRunning(gfx);
    } else {
        drawPicker(gfx);
    }
}

void PlaybookScreen::drawPicker(M5Canvas& gfx) {
    size_t count = PlaybookRunner::playbookCount();
    constexpr int16_t kRowH = 24;
    constexpr int16_t kTop = 18;

    for (size_t i = 0; i < count; i++) {
        int16_t y = kTop + (int16_t)i * kRowH;
        bool sel = (i == _selected);
        uint16_t rowBg = sel ? theme::PANEL_BG : theme::BG;
        gfx.drawRect(4, y, gfx.width() - 8, kRowH - 2, sel ? theme::CYAN : theme::GREY);
        gfx.fillRect(5, y + 1, gfx.width() - 10, kRowH - 4, rowBg);

        gfx.setTextColor(sel ? theme::MAGENTA : theme::GREY, rowBg);
        gfx.setCursor(10, y + 3);
        gfx.print(sel ? "> " : "  ");
        gfx.setTextColor(sel ? theme::CYAN : theme::GREEN, rowBg);
        gfx.print(PlaybookRunner::playbookName(i));

        gfx.setTextColor(theme::GREY, rowBg);
        gfx.setCursor(10, y + 13);
        String desc = PlaybookRunner::playbookDescription(i);
        if (desc.length() > 36) desc = desc.substring(0, 36);
        gfx.print(desc);
    }

    gfx.setTextColor(theme::GREY, theme::BG);
    gfx.setCursor(4, gfx.height() - 9);
    gfx.print("ENTER:run  Arrows:pick  DEL:back");
}

void PlaybookScreen::drawRunning(M5Canvas& gfx) {
    size_t pbIndex = g_playbookRunner.currentPlaybook();
    size_t step = g_playbookRunner.currentStep();
    size_t stepCount = PlaybookRunner::playbookStepCount(pbIndex);

    gfx.setTextColor(theme::GREEN, theme::BG);
    gfx.setCursor(6, 18);
    gfx.print(PlaybookRunner::playbookName(pbIndex));

    gfx.setTextColor(theme::CYAN, theme::BG);
    gfx.setCursor(6, 29);
    gfx.print("step ");
    gfx.print((unsigned)(step < stepCount ? step + 1 : stepCount));
    gfx.print("/");
    gfx.print((unsigned)stepCount);
    if (step < stepCount) {
        gfx.print(": ");
        String label = PlaybookRunner::playbookStepLabel(pbIndex, step);
        if (label.length() > 22) label = label.substring(0, 22);
        gfx.print(label);
    }

    // Progress bar, derived from step index/count — coarser than
    // AssessmentRunner/DiscoveryRunner's own percent-within-step bars
    // (a playbook step is itself an opaque black box from up here), but
    // enough to show real motion across a multi-step run.
    uint8_t pct = (stepCount > 0) ? (uint8_t)((step * 100) / stepCount) : 0;
    int16_t barX = 6, barY = 40, barW = gfx.width() - 12, barH = 8;
    gfx.drawRect(barX, barY, barW, barH, theme::GREY);
    int16_t fillW = (int16_t)((int32_t)(barW - 2) * pct / 100);
    if (fillW > 0) gfx.fillRect(barX + 1, barY + 1, fillW, barH - 2, theme::CYAN);

    gfx.setTextColor(theme::GREY, theme::BG);
    gfx.drawFastHLine(4, 54, gfx.width() - 8, theme::GREY);

    for (uint8_t i = 0; i < _logCount; i++) {
        int16_t y = 57 + i * 9;
        gfx.setTextColor(theme::AMBER, theme::BG);
        gfx.setCursor(6, y);
        String line = _log[i];
        if (line.length() > 37) line = line.substring(0, 37);
        gfx.print(line);
    }

    gfx.setTextColor(theme::MAGENTA, theme::BG);
    gfx.setCursor(6, gfx.height() - 20);
    gfx.print("ENTER: stop");

    gfx.setTextColor(theme::GREY, theme::BG);
    gfx.setCursor(4, gfx.height() - 9);
    gfx.print("DEL:back (keeps running)");
}
