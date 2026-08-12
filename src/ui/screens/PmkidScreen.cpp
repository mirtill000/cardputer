#include "PmkidScreen.h"
#include "../UiManager.h"
#include "../Theme.h"
#include "../Chrome.h"
#include "../../scan/PmkidManager.h"

PmkidScreen& PmkidScreen::instance() {
    static PmkidScreen s;
    return s;
}

void PmkidScreen::setTarget(const String& ssid, const String& bssid, uint8_t channel) {
    _ssid = ssid;
    _bssid = bssid;
    _channel = channel;
}

void PmkidScreen::onEnter() {
    _state = State::Idle;
    _logCount = 0;
}

void PmkidScreen::pushLog(const String& line) {
    if (_logCount < kLogLines) {
        _log[_logCount++] = line;
    } else {
        for (uint8_t i = 1; i < kLogLines; i++) _log[i - 1] = _log[i];
        _log[kLogLines - 1] = line;
    }
}

void PmkidScreen::onScanEvent(const ScanNotification& ev) {
    if (ev.source != ScanSource::Pmkid) return;
    if (ev.type == ScanEventType::LogLine) pushLog(String(ev.text));
    if (_state == State::Running && !g_pmkidManager.isRunning()) _state = State::Done;
}

void PmkidScreen::onKey(UiKey key, char /*ch*/) {
    switch (_state) {
        case State::Idle:
            if (key == UiKey::Enter) {
                _logCount = 0;
                if (g_pmkidManager.start(_ssid, _bssid, _channel)) {
                    _state = State::Running;
                } else {
                    pushLog("start failed");
                }
            } else if (key == UiKey::Back) {
                g_ui.popScreen();
            }
            break;

        case State::Running:
            if (key == UiKey::Back) g_ui.popScreen();  // capture keeps running to completion in background
            break;

        case State::Done:
            if (key == UiKey::Back || key == UiKey::Enter) g_ui.popScreen();
            break;
    }
}

void PmkidScreen::draw(M5Canvas& gfx) {
    gfx.fillScreen(theme::BG);
    chrome::drawHeader(gfx, "PMKID CAPTURE");

    gfx.setTextColor(theme::GREEN, theme::BG);
    gfx.setCursor(6, 18);
    gfx.print(_ssid.length() ? _ssid : String("<hidden>"));
    gfx.setTextColor(theme::GREY, theme::BG);
    gfx.setCursor(6, 28);
    gfx.print(_bssid);
    gfx.print(" ch");
    gfx.print(_channel);

    switch (_state) {
        case State::Idle:
            gfx.setTextColor(theme::AMBER, theme::BG);
            gfx.setCursor(6, 44);
            gfx.print("No deauth - just associates with");
            gfx.setCursor(6, 54);
            gfx.print("a wrong password and captures");
            gfx.setCursor(6, 64);
            gfx.print("whatever EAPOL the AP sends first.");

            gfx.setTextColor(theme::MAGENTA, theme::BG);
            gfx.setCursor(6, gfx.height() - 20);
            gfx.print("ENTER: start capture");
            gfx.setTextColor(theme::GREY, theme::BG);
            gfx.setCursor(4, gfx.height() - 9);
            gfx.print("DEL:back");
            break;

        case State::Running: {
            gfx.setTextColor(theme::CYAN, theme::BG);
            gfx.setCursor(6, 44);
            gfx.print("captured: ");
            gfx.print((unsigned)g_pmkidManager.capturedPackets());

            gfx.setTextColor(theme::GREY, theme::BG);
            gfx.drawFastHLine(4, 54, gfx.width() - 8, theme::GREY);
            for (uint8_t i = 0; i < _logCount; i++) {
                int16_t y = 57 + i * 9;
                gfx.setTextColor(theme::GREEN, theme::BG);
                gfx.setCursor(6, y);
                String line = _log[i];
                if (line.length() > 37) line = line.substring(0, 37);
                gfx.print(line);
            }

            gfx.setTextColor(theme::GREY, theme::BG);
            gfx.setCursor(4, gfx.height() - 9);
            gfx.print("DEL:back (capture continues)");
            break;
        }

        case State::Done:
            gfx.setTextColor(theme::MAGENTA, theme::BG);
            gfx.setCursor(6, 44);
            gfx.print("capture finished");
            gfx.setTextColor(theme::GREEN, theme::BG);
            gfx.setCursor(6, 56);
            gfx.print(String((unsigned)g_pmkidManager.capturedPackets()) + " packets saved to:");
            gfx.setCursor(6, 66);
            {
                String path = g_pmkidManager.pcapPath();
                if (path.length() > 37) path = path.substring(0, 37);
                gfx.print(path);
            }

            gfx.setTextColor(theme::GREY, theme::BG);
            gfx.setCursor(6, 80);
            gfx.print("check offline (hashcat -m 22000)");

            gfx.setCursor(4, gfx.height() - 9);
            gfx.print("ENTER/DEL:back");
            break;
    }
}
