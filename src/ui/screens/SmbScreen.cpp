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
    chrome::drawHeader(gfx, "SMB POSTURE");

    gfx.setTextColor(theme::GREEN, theme::BG);
    gfx.setCursor(6, 18);
    gfx.print("target: ");
    gfx.print(_target.toString());
    gfx.print(":");
    gfx.print(_port);

    SmbNegotiateCheck::Result r = g_smbCheck.result();
    bool running = g_smbCheck.isRunning();

    if (!r.done) {
        gfx.setTextColor(theme::GREY, theme::BG);
        gfx.setCursor(6, 32);
        gfx.print(running ? "probing SMB1 + SMB2..." : "not run yet");
    } else {
        // Overall verdict.
        uint16_t pc = (r.posture == SmbNegotiateCheck::Posture::Weak)   ? theme::RED
                    : (r.posture == SmbNegotiateCheck::Posture::Fair)   ? theme::AMBER
                    : (r.posture == SmbNegotiateCheck::Posture::Ok)     ? theme::GREEN
                                                                        : theme::GREY;
        const char* pl = (r.posture == SmbNegotiateCheck::Posture::Weak)   ? "WEAK"
                       : (r.posture == SmbNegotiateCheck::Posture::Fair)   ? "FAIR"
                       : (r.posture == SmbNegotiateCheck::Posture::Ok)     ? "OK"
                                                                           : "?";
        gfx.setTextColor(pc, theme::BG);
        gfx.setCursor(6, 30);
        gfx.print("POSTURE: ");
        gfx.print(pl);

        // SMBv1 exposure.
        gfx.setCursor(6, 42);
        if (r.smb1Enabled) {
            gfx.setTextColor(theme::RED, theme::BG);
            gfx.print("SMBv1: ENABLED (legacy)");
        } else if (r.connected) {
            gfx.setTextColor(theme::GREEN, theme::BG);
            gfx.print("SMBv1: not offered");
        } else {
            gfx.setTextColor(theme::GREY, theme::BG);
            gfx.print("SMBv1: n/a");
        }

        // SMB2 dialect + signing.
        gfx.setCursor(6, 52);
        if (r.smb2Supported) {
            gfx.setTextColor(r.smb2SigningRequired ? theme::GREEN : theme::AMBER, theme::BG);
            String s = String("SMB2: ") + SmbNegotiateCheck::dialectName(r.smb2Dialect) +
                       (r.smb2SigningRequired ? " sign:req"
                                              : (r.smb2SigningEnabled ? " sign:opt" : " sign:off"));
            gfx.print(s);
        } else {
            gfx.setTextColor(theme::AMBER, theme::BG);
            gfx.print("SMB2: none");
        }

        // Legacy SMB1 detail line - only meaningful when SMBv1 answered.
        if (r.smb1Enabled) {
            gfx.setCursor(6, 62);
            gfx.setTextColor(theme::GREY, theme::BG);
            String d = String("v1: ") + (r.userLevelSecurity ? "user" : "SHARE") + "/" +
                       (r.challengeResponse ? "c-r" : "PLAIN") + "/" +
                       (r.signingRequired ? "signed" : (r.signingEnabled ? "sign-opt" : "unsigned"));
            gfx.print(d);
        }
    }

    gfx.setTextColor(theme::GREY, theme::BG);
    gfx.drawFastHLine(4, 71, gfx.width() - 8, theme::GREY);

    // Most-recent log lines (bottom of the array is newest - see pushLog).
    uint8_t show = (_logCount < 4) ? _logCount : 4;
    uint8_t startIdx = (uint8_t)(_logCount - show);
    for (uint8_t i = 0; i < show; i++) {
        int16_t y = 74 + i * 9;
        gfx.setTextColor(theme::CYAN, theme::BG);
        gfx.setCursor(6, y);
        String line = _log[startIdx + i];
        if (line.length() > 37) line = line.substring(0, 37);
        gfx.print(line);
    }

    gfx.setTextColor(theme::MAGENTA, theme::BG);
    gfx.setCursor(6, gfx.height() - 20);
    gfx.print(running ? "probing..." : "ENTER: SMB posture scan");

    gfx.setTextColor(theme::GREY, theme::BG);
    gfx.setCursor(4, gfx.height() - 9);
    gfx.print("DEL:back");
}
