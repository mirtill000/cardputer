#include "DeauthScreen.h"
#include "../UiManager.h"
#include "../Theme.h"
#include "../Chrome.h"
#include "../../scan/DeauthManager.h"

DeauthScreen& DeauthScreen::instance() {
    static DeauthScreen s;
    return s;
}

void DeauthScreen::setTarget(const String& ssid, const String& bssid, uint8_t channel) {
    _ssid = ssid;
    _bssid = bssid;
    _channel = channel;
}

void DeauthScreen::onEnter() {
    _state = State::EnterClientMac;
    _clientMacText = "";
    _logCount = 0;
    g_ui.setTextEntryMode(true);
}

void DeauthScreen::onExit() {
    g_ui.setTextEntryMode(false);
}

void DeauthScreen::pushLog(const String& line) {
    if (_logCount < kLogLines) {
        _log[_logCount++] = line;
    } else {
        for (uint8_t i = 1; i < kLogLines; i++) _log[i - 1] = _log[i];
        _log[kLogLines - 1] = line;
    }
}

void DeauthScreen::onScanEvent(const ScanNotification& ev) {
    if (ev.source != ScanSource::Deauth) return;
    if (ev.type == ScanEventType::LogLine) pushLog(String(ev.text));
    if (_state == State::Running && !g_deauthManager.isRunning()) _state = State::Done;
}

void DeauthScreen::onKey(UiKey key, char ch) {
    switch (_state) {
        case State::EnterClientMac:
            if (key == UiKey::Char) {
                if (_clientMacText.length() < 17) _clientMacText += ch;
            } else if (key == UiKey::Back) {
                if (_clientMacText.length() > 0) {
                    _clientMacText.remove(_clientMacText.length() - 1);
                } else {
                    g_ui.popScreen();
                }
            } else if (key == UiKey::Enter) {
                uint8_t mac[6];
                if (parseMacString(_clientMacText, mac)) {
                    g_ui.setTextEntryMode(false);
                    _logCount = 0;
                    if (g_deauthManager.start(_bssid, _channel, _clientMacText)) {
                        _state = State::Running;
                    } else {
                        pushLog("start failed - check the MAC format");
                    }
                }
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

void DeauthScreen::draw(M5Canvas& gfx) {
    gfx.fillScreen(theme::BG);
    chrome::drawHeader(gfx, "DEAUTH + CAPTURE");

    gfx.setTextColor(theme::GREEN, theme::BG);
    gfx.setCursor(6, 18);
    gfx.print(_ssid.length() ? _ssid : String("<hidden>"));
    gfx.setTextColor(theme::GREY, theme::BG);
    gfx.setCursor(6, 28);
    gfx.print(_bssid);
    gfx.print(" ch");
    gfx.print(_channel);

    switch (_state) {
        case State::EnterClientMac: {
            gfx.setTextColor(theme::CYAN, theme::BG);
            gfx.setCursor(6, 44);
            gfx.print("client MAC to deauth:");

            gfx.fillRect(6, 56, gfx.width() - 12, 10, theme::PANEL_BG);
            gfx.setTextColor(theme::GREEN_BRIGHT, theme::PANEL_BG);
            gfx.setCursor(8, 57);
            gfx.print(_clientMacText);

            gfx.setTextColor(theme::RED, theme::BG);
            gfx.setCursor(6, 72);
            gfx.print("aa:bb:cc:dd:ee:ff format, ONE client");

            gfx.setTextColor(theme::GREY, theme::BG);
            gfx.setCursor(4, gfx.height() - 9);
            gfx.print("ENTER:start DEL:erase/back");
            break;
        }

        case State::Running: {
            gfx.setTextColor(theme::CYAN, theme::BG);
            gfx.setCursor(6, 44);
            gfx.print("sent:");
            gfx.print((unsigned)g_deauthManager.framesSent());
            gfx.print(" captured:");
            gfx.print((unsigned)g_deauthManager.capturedPackets());
            if (g_deauthManager.handshakeLikelyCaptured()) {
                gfx.setTextColor(theme::GREEN, theme::BG);
                gfx.print(" OK!");
            }

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

        case State::Done: {
            bool likely = g_deauthManager.handshakeLikelyCaptured();
            gfx.setTextColor(likely ? theme::GREEN : theme::AMBER, theme::BG);
            gfx.setCursor(6, 44);
            gfx.print(likely ? "handshake likely captured!" : "no full handshake seen");

            gfx.setTextColor(theme::GREY, theme::BG);
            gfx.setCursor(6, 56);
            gfx.print(String((unsigned)g_deauthManager.capturedPackets()) + " packets saved to:");
            gfx.setCursor(6, 66);
            String path = g_deauthManager.pcapPath();
            if (path.length() > 37) path = path.substring(0, 37);
            gfx.print(path);

            gfx.setTextColor(theme::GREY, theme::BG);
            gfx.setCursor(6, 80);
            gfx.print(likely ? "verify offline: hashcat -m 22000" : "client may not have reconnected");

            gfx.setCursor(4, gfx.height() - 9);
            gfx.print("ENTER/DEL:back");
            break;
        }
    }
}
