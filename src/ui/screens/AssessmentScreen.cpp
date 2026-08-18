#include "AssessmentScreen.h"
#include "../UiManager.h"
#include "../Theme.h"
#include "../Chrome.h"
#include "../../scan/AssessmentRunner.h"

AssessmentScreen& AssessmentScreen::instance() {
    static AssessmentScreen s;
    return s;
}

void AssessmentScreen::onEnter() {
    _logCount = 0;
}

void AssessmentScreen::pushLog(const String& line) {
    if (_logCount < kLogLines) {
        _log[_logCount++] = line;
    } else {
        for (uint8_t i = 1; i < kLogLines; i++) _log[i - 1] = _log[i];
        _log[kLogLines - 1] = line;
    }
}

void AssessmentScreen::onScanEvent(const ScanNotification& ev) {
    if (ev.source != ScanSource::Assessment) return;
    if (ev.type == ScanEventType::LogLine) pushLog(String(ev.text));
    else if (ev.type == ScanEventType::ScanStarted) _logCount = 0;
}

void AssessmentScreen::onKey(UiKey key, char /*ch*/) {
    if (key == UiKey::Enter) {
        if (g_assessmentRunner.isRunning()) {
            g_assessmentRunner.stop();
        } else {
            g_assessmentRunner.start();
        }
        return;
    }
    if (key == UiKey::Back) g_ui.popScreen();  // keeps running in the background
}

namespace {
const char* phaseLabel(AssessmentRunner::Phase p) {
    switch (p) {
        case AssessmentRunner::Phase::Discovery: return "DISCOVERY";
        case AssessmentRunner::Phase::PortScan: return "PORT SCAN";
        case AssessmentRunner::Phase::Report: return "REPORT";
        case AssessmentRunner::Phase::Done: return "DONE";
        case AssessmentRunner::Phase::Failed: return "FAILED";
        default: return "IDLE";
    }
}
}  // namespace

void AssessmentScreen::draw(M5Canvas& gfx) {
    gfx.fillScreen(theme::BG);
    chrome::drawHeader(gfx, "AUTO ASSESS");

    bool running = g_assessmentRunner.isRunning();
    AssessmentRunner::Phase phase = g_assessmentRunner.phase();

    gfx.setTextColor(theme::GREEN, theme::BG);
    gfx.setCursor(6, 18);
    gfx.print("phase: ");
    uint16_t pc = (phase == AssessmentRunner::Phase::Failed) ? theme::RED
                  : (phase == AssessmentRunner::Phase::Done) ? theme::GREEN
                                                             : theme::CYAN;
    gfx.setTextColor(pc, theme::BG);
    gfx.print(phaseLabel(phase));

    // Progress bar.
    uint8_t pct = g_assessmentRunner.progressPct();
    int16_t barX = 6, barY = 30, barW = gfx.width() - 12, barH = 8;
    gfx.drawRect(barX, barY, barW, barH, theme::GREY);
    int16_t fillW = (int16_t)((int32_t)(barW - 2) * pct / 100);
    if (fillW > 0) gfx.fillRect(barX + 1, barY + 1, fillW, barH - 2, theme::CYAN);
    gfx.setTextColor(theme::GREY, theme::BG);
    gfx.setCursor(barX + barW - 24, barY + barH + 3);
    gfx.print(pct);
    gfx.print("%");

    if (g_assessmentRunner.hostsTotal() > 0) {
        gfx.setTextColor(theme::GREEN, theme::BG);
        gfx.setCursor(6, 41);
        gfx.print("hosts: ");
        gfx.print((unsigned)g_assessmentRunner.hostsDone());
        gfx.print("/");
        gfx.print((unsigned)g_assessmentRunner.hostsTotal());
    }

    gfx.setTextColor(theme::GREY, theme::BG);
    gfx.drawFastHLine(4, 52, gfx.width() - 8, theme::GREY);

    for (uint8_t i = 0; i < _logCount; i++) {
        int16_t y = 55 + i * 9;
        gfx.setTextColor(theme::AMBER, theme::BG);
        gfx.setCursor(6, y);
        String line = _log[i];
        if (line.length() > 37) line = line.substring(0, 37);
        gfx.print(line);
    }

    if (phase == AssessmentRunner::Phase::Done && g_assessmentRunner.reportOk()) {
        gfx.setTextColor(theme::GREEN, theme::BG);
        gfx.setCursor(6, gfx.height() - 20);
        String rp = g_assessmentRunner.reportPath();
        if (rp.length() > 37) rp = rp.substring(0, 37);
        gfx.print(rp);
    } else {
        gfx.setTextColor(theme::MAGENTA, theme::BG);
        gfx.setCursor(6, gfx.height() - 20);
        gfx.print(running ? "ENTER: stop" : "ENTER: run full assessment");
    }

    gfx.setTextColor(theme::GREY, theme::BG);
    gfx.setCursor(4, gfx.height() - 9);
    gfx.print("DEL:back (keeps running)");
}
