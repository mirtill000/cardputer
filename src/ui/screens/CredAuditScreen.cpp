#include "CredAuditScreen.h"
#include "../TextWrap.h"
#include "../UiManager.h"
#include "../Theme.h"
#include "../Chrome.h"
#include "../../core/Types.h"
#include "../../scan/CredAuditManager.h"
#include "../../scan/ScanManager.h"

CredAuditScreen& CredAuditScreen::instance() {
    static CredAuditScreen s;
    return s;
}

void CredAuditScreen::onEnter() {
    _logCount = 0;
}

void CredAuditScreen::pushLog(const String& line) {
    if (_logCount < kLogLines) {
        _log[_logCount++] = line;
    } else {
        for (uint8_t i = 1; i < kLogLines; i++) _log[i - 1] = _log[i];
        _log[kLogLines - 1] = line;
    }
}

void CredAuditScreen::onScanEvent(const ScanNotification& ev) {
    if (ev.source != ScanSource::CredAudit) return;  // not ours — see ScanSource in EventQueue.h
    if (ev.type == ScanEventType::LogLine) {
        pushLog(String(ev.text));
    } else if (ev.type == ScanEventType::ScanStarted) {
        _logCount = 0;
    }
}

void CredAuditScreen::onKey(UiKey key, char /*ch*/) {
    if (key == UiKey::Enter) {
        if (!g_credAuditManager.isRunning()) {
            g_credAuditManager.startAudit(_target);
        }
        return;
    }
    if (key == UiKey::Back) {
        g_ui.popScreen();
    }
}

void CredAuditScreen::draw(M5Canvas& gfx) {
    gfx.fillScreen(theme::BG);
    chrome::drawHeader(gfx, "CREDENTIAL GUESS");

    gfx.setTextColor(theme::GREEN, theme::BG);
    gfx.setCursor(6, 18);
    gfx.print("target: ");
    gfx.print(_target.toString());

    bool running = g_credAuditManager.isRunning();
    bool sameTarget = (g_credAuditManager.target() == _target);

    if (running && sameTarget) {
        gfx.setTextColor(theme::CYAN, theme::BG);
        gfx.setCursor(6, 28);
        gfx.print("attempts:");
        gfx.print(g_credAuditManager.attemptCount());
        gfx.print(" success:");
        gfx.print(g_credAuditManager.successCount());

        gfx.setTextColor(theme::GREY, theme::BG);
        gfx.drawFastHLine(4, 39, gfx.width() - 8, theme::GREY);

        for (uint8_t i = 0; i < _logCount; i++) {
            int16_t y = 42 + i * 9;
            bool isFail = _log[i].endsWith("FAIL");
            gfx.setTextColor(isFail ? theme::GREY : theme::RED, theme::BG);
            gfx.setCursor(6, y);
            String line = _log[i];
            if (line.length() > 37) line = line.substring(0, 37);
            gfx.print(line);
        }

        gfx.setTextColor(theme::GREY, theme::BG);
        gfx.setCursor(4, gfx.height() - 9);
        gfx.print("DEL:back (keeps running)");
        return;
    }

    HostInfo h;
    bool found = g_scanManager.getHostByIp(_target, h);

    if (found && h.credAudited) {
        if (h.credVulnerable) {
            gfx.setTextColor(theme::RED, theme::BG);
            gfx.setCursor(6, 30);
            gfx.print("!! CREDENTIALS ACCEPTED !!");
        } else {
            gfx.setTextColor(theme::GREEN, theme::BG);
            gfx.setCursor(6, 30);
            gfx.print("no working credentials found");
        }
        gfx.setTextColor(theme::GREY, theme::BG);
        drawWrapped(gfx, h.credNote.c_str(), 6, 42, 10, 37);

        gfx.setTextColor(theme::CYAN, theme::BG);
        gfx.setCursor(6, gfx.height() - 30);
        gfx.print("last run: ");
        gfx.print(g_credAuditManager.attemptCount());
        gfx.print(" attempts");
    } else {
        gfx.setTextColor(theme::GREEN, theme::BG);
        gfx.setCursor(6, 30);
        gfx.print("not checked yet this session");
    }

    gfx.setTextColor(theme::MAGENTA, theme::BG);
    gfx.setCursor(6, gfx.height() - 20);
    gfx.print(found && h.credAudited ? "ENTER: re-run" : "ENTER: run attack");

    gfx.setTextColor(theme::GREY, theme::BG);
    gfx.setCursor(4, gfx.height() - 9);
    gfx.print("DEL:back");
}
