#include "OpenConnectScreen.h"
#include "../UiManager.h"
#include "../Theme.h"
#include "../Chrome.h"
#include "../TextWrap.h"
#include "../../net/WifiManager.h"
#include "../../net/CaptivePortalDetector.h"

OpenConnectScreen& OpenConnectScreen::instance() {
    static OpenConnectScreen s;
    return s;
}

void OpenConnectScreen::onEnter() {
    _state = State::Confirm;
    _connectStartMs = 0;
    _detectStarted = false;
    _logCount = 0;
}

void OpenConnectScreen::pushLog(const String& line) {
    if (_logCount < kLogLines) {
        _log[_logCount++] = line;
    } else {
        for (uint8_t i = 1; i < kLogLines; i++) _log[i - 1] = _log[i];
        _log[kLogLines - 1] = line;
    }
}

void OpenConnectScreen::onScanEvent(const ScanNotification& ev) {
    if (ev.source != ScanSource::CaptivePortal) return;
    if (ev.type == ScanEventType::LogLine) pushLog(String(ev.text));
}

void OpenConnectScreen::onKey(UiKey key, char ch) {
    if (_state == State::Confirm) {
        if (key == UiKey::Char && (ch == 'y' || ch == 'Y')) {
            g_wifi.beginConnectWithCredentials(_ssid, "");  // open network: empty password
            _connectStartMs = millis();
            _state = State::Connecting;
            pushLog("connecting...");
        } else if (key == UiKey::Back) {
            g_ui.popScreen();
        }
        return;
    }
    // In every later state DEL just leaves (the network stays joined).
    if (key == UiKey::Back) g_ui.popScreen();
}

void OpenConnectScreen::update(uint32_t /*nowMs*/) {
    if (_state == State::Connecting) {
        if (g_wifi.isConnected() && g_wifi.currentSsid() == _ssid) {
            _state = State::Detecting;
            _detectStarted = g_captivePortalDetector.start();
            if (!_detectStarted) {  // couldn't start (shouldn't happen right after connect)
                _state = State::Done;
            }
        } else if (g_wifi.connectFailed() || (millis() - _connectStartMs) > kConnectTimeoutMs) {
            _state = State::Failed;
        }
    } else if (_state == State::Detecting) {
        if (!g_captivePortalDetector.isRunning() &&
            g_captivePortalDetector.result().status != CaptivePortalDetector::Status::Checking) {
            _state = State::Done;
        }
    }
}

void OpenConnectScreen::draw(M5Canvas& gfx) {
    gfx.fillScreen(theme::BG);
    chrome::drawHeader(gfx, "JOIN OPEN NET");

    if (_state == State::Confirm) {
        gfx.setTextColor(theme::AMBER, theme::BG);
        gfx.setCursor(6, 20);
        gfx.print("network: ");
        gfx.setTextColor(theme::CYAN, theme::BG);
        String s = _ssid;
        if (s.length() > 26) s = s.substring(0, 26);
        gfx.print(s);

        gfx.setTextColor(theme::AMBER, theme::BG);
        drawWrapped(gfx,
                    "Join this OPEN network? Your device will connect to a third-party network. "
                    "Only do this on networks you own or are explicitly authorized to use. "
                    "A captive-portal check will run after connecting (detection only).",
                    6, 34, 10, 37);

        gfx.setTextColor(theme::MAGENTA, theme::BG);
        gfx.setCursor(6, 112);
        gfx.print("Y: authorized, connect");
        gfx.setTextColor(theme::GREY, theme::BG);
        gfx.setCursor(6, 122);
        gfx.print("DEL: cancel");
        return;
    }

    // Status line for the connect/detect flow.
    gfx.setTextColor(theme::GREEN, theme::BG);
    gfx.setCursor(6, 20);
    gfx.print("net: ");
    gfx.setTextColor(theme::CYAN, theme::BG);
    gfx.print(g_wifi.currentSsid().length() ? g_wifi.currentSsid() : _ssid);

    gfx.setCursor(6, 32);
    if (_state == State::Connecting) {
        gfx.setTextColor(theme::AMBER, theme::BG);
        gfx.print("connecting...");
    } else if (_state == State::Failed) {
        gfx.setTextColor(theme::RED, theme::BG);
        gfx.print("connection failed");
    } else {
        gfx.setTextColor(theme::GREEN, theme::BG);
        gfx.print("connected  ");
        gfx.print(g_wifi.localIP().toString());
    }

    // Captive-portal verdict.
    if (_state == State::Detecting || _state == State::Done) {
        CaptivePortalDetector::Result r = g_captivePortalDetector.result();
        gfx.setCursor(6, 46);
        switch (r.status) {
            case CaptivePortalDetector::Status::Checking:
                gfx.setTextColor(theme::AMBER, theme::BG);
                gfx.print("checking captive portal...");
                break;
            case CaptivePortalDetector::Status::OpenInternet:
                gfx.setTextColor(theme::GREEN, theme::BG);
                gfx.print("no captive portal - open internet");
                break;
            case CaptivePortalDetector::Status::PortalDetected:
                gfx.setTextColor(theme::RED, theme::BG);
                gfx.print("CAPTIVE PORTAL detected");
                if (r.portalUrl.length()) {
                    gfx.setTextColor(theme::GREY, theme::BG);
                    gfx.setCursor(6, 58);
                    String u = r.portalUrl;
                    if (u.length() > 37) u = u.substring(0, 37);
                    gfx.print(u);
                    gfx.setTextColor(theme::AMBER, theme::BG);
                    gfx.setCursor(6, 70);
                    gfx.print("sign in via a browser to proceed");
                }
                break;
            default:
                gfx.setTextColor(theme::AMBER, theme::BG);
                gfx.print("no connectivity to check endpoint");
                break;
        }
    }

    // Recent log lines.
    gfx.setTextColor(theme::GREY, theme::BG);
    gfx.drawFastHLine(4, 84, gfx.width() - 8, theme::GREY);
    for (uint8_t i = 0; i < _logCount; i++) {
        int16_t y = 88 + i * 9;
        gfx.setTextColor(theme::GREEN, theme::BG);
        gfx.setCursor(6, y);
        String line = _log[i];
        if (line.length() > 37) line = line.substring(0, 37);
        gfx.print(line);
    }

    gfx.setTextColor(theme::GREY, theme::BG);
    gfx.setCursor(4, gfx.height() - 9);
    gfx.print("DEL:back (stays connected)");
}
