#include "SmbScreen.h"
#include "../UiManager.h"
#include "../Theme.h"
#include "../Chrome.h"
#include "../../scan/SmbNegotiateCheck.h"

SmbScreen& SmbScreen::instance() {
    static SmbScreen s;
    return s;
}

void SmbScreen::setTarget(const IPAddress& ip, uint16_t port) {
    _target = ip;
    _port = port;
}

void SmbScreen::onEnter() {
    _logCount = 0;
}

void SmbScreen::pushLog(const String& line) {
    if (_logCount < kLogLines) {
        _log[_logCount++] = line;
    } else {
        for (uint8_t i = 1; i < kLogLines; i++) _log[i - 1] = _log[i];
        _log[kLogLines - 1] = line;
    }
}

void SmbScreen::onScanEvent(const ScanNotification& ev) {
    if (ev.source != ScanSource::Smb) return;
    if (ev.type == ScanEventType::LogLine) pushLog(String(ev.text));
    else if (ev.type == ScanEventType::ScanStarted) _logCount = 0;
}

void SmbScreen::onKey(UiKey key, char /*ch*/) {
    if (key == UiKey::Enter) {
        if (!g_smbCheck.isRunning()) g_smbCheck.start(_target, _port);
        return;
    }
    if (key == UiKey::Back) g_ui.popScreen();
}

void SmbScreen::draw(M5Canvas& gfx) {
    gfx.fillScreen(theme::BG);
    chrome::drawHeader(gfx, "SMB NEGOTIATE");

    gfx.setTextColor(theme::GREEN, theme::BG);
    gfx.setCursor(6, 18);
    gfx.print("target: ");
    gfx.print(_target.toString());
    gfx.print(":");
    gfx.print(_port);

    SmbNegotiateCheck::Result r = g_smbCheck.result();
    bool running = g_smbCheck.isRunning();

    gfx.setCursor(6, 28);
    if (r.done && r.negotiated) {
        // Flag the two legacy/weak states in red, the rest green.
        uint16_t modeColor = r.userLevelSecurity ? theme::GREEN : theme::RED;
        gfx.setTextColor(modeColor, theme::BG);
        gfx.print(r.userLevelSecurity ? "security: user-level" : "security: SHARE-LEVEL");
        gfx.setCursor(6, 38);
        gfx.setTextColor(r.challengeResponse ? theme::GREEN : theme::RED, theme::BG);
        gfx.print(r.challengeResponse ? "passwords: challenge/resp" : "passwords: PLAINTEXT");
        gfx.setCursor(6, 48);
        gfx.setTextColor(r.signingRequired ? theme::GREEN : theme::AMBER, theme::BG);
        gfx.print(r.signingRequired ? "signing: required"
                                    : (r.signingEnabled ? "signing: optional" : "signing: off"));
    } else if (r.done) {
        gfx.setTextColor(theme::AMBER, theme::BG);
        gfx.print(r.note.length() ? r.note : String("no SMB1 negotiate"));
    } else {
        gfx.setTextColor(theme::GREY, theme::BG);
        gfx.print(running ? "negotiating..." : "not run yet");
    }

    gfx.setTextColor(theme::GREY, theme::BG);
    gfx.drawFastHLine(4, 59, gfx.width() - 8, theme::GREY);

    for (uint8_t i = 0; i < _logCount; i++) {
        int16_t y = 62 + i * 9;
        gfx.setTextColor(theme::CYAN, theme::BG);
        gfx.setCursor(6, y);
        String line = _log[i];
        if (line.length() > 37) line = line.substring(0, 37);
        gfx.print(line);
    }

    gfx.setTextColor(theme::MAGENTA, theme::BG);
    gfx.setCursor(6, gfx.height() - 20);
    gfx.print(running ? "probing..." : "ENTER: SMB negotiate");

    gfx.setTextColor(theme::GREY, theme::BG);
    gfx.setCursor(4, gfx.height() - 9);
    gfx.print("DEL:back");
}
