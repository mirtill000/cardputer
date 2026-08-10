#include "CredAuditScreen.h"
#include "../TextWrap.h"
#include "../UiManager.h"
#include "../Theme.h"
#include "../../core/Types.h"
#include "../../scan/CredAuditManager.h"
#include "../../scan/ScanManager.h"

CredAuditScreen& CredAuditScreen::instance() {
    static CredAuditScreen s;
    return s;
}

void CredAuditScreen::onScanEvent(const ScanNotification& ev) {
    if (ev.source != ScanSource::CredAudit) return;  // not ours — see ScanSource in EventQueue.h
    (void)ev;  // state is re-read from CredAuditManager/ScanManager each draw(), nothing to cache here
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
    gfx.setTextColor(theme::GREEN_BRIGHT, theme::BG);
    gfx.setCursor(4, 4);
    gfx.print(">> CRED AUDIT ");
    gfx.print(_target.toString());
    gfx.drawFastHLine(4, 15, gfx.width() - 8, theme::GREY);

    bool running = g_credAuditManager.isRunning();
    bool sameTarget = (g_credAuditManager.target() == _target);

    if (running && sameTarget) {
        gfx.setTextColor(theme::CYAN, theme::BG);
        gfx.setCursor(6, 30);
        gfx.print("checking default credentials...");
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
            gfx.setCursor(6, 24);
            gfx.print("!! DEFAULT CREDS ACCEPTED !!");
        } else {
            gfx.setTextColor(theme::GREEN, theme::BG);
            gfx.setCursor(6, 24);
            gfx.print("dictionary check: clean");
        }
        gfx.setTextColor(theme::GREY, theme::BG);
        drawWrapped(gfx, h.credNote.c_str(), 6, 36, 10, 37);
    } else {
        gfx.setTextColor(theme::GREEN, theme::BG);
        gfx.setCursor(6, 30);
        gfx.print("not checked yet this session");
    }

    gfx.setTextColor(theme::MAGENTA, theme::BG);
    gfx.setCursor(6, gfx.height() - 20);
    gfx.print(found && h.credAudited ? "ENTER: re-check" : "ENTER: run check");

    gfx.setTextColor(theme::GREY, theme::BG);
    gfx.setCursor(4, gfx.height() - 9);
    gfx.print("DEL:back");
}
