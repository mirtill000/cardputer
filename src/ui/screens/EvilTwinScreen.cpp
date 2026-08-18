#include "EvilTwinScreen.h"
#include "../UiManager.h"
#include "../Theme.h"
#include "../Chrome.h"
#include "../../scan/EvilTwinManager.h"

EvilTwinScreen& EvilTwinScreen::instance() {
    static EvilTwinScreen s;
    return s;
}

void EvilTwinScreen::setSuggestedSsid(const String& ssid, uint8_t channel) {
    _ssidText = ssid;
    _channel = channel ? channel : 1;
}

void EvilTwinScreen::onEnter() {
    _state = g_evilTwinManager.isRunning() ? State::Running : State::EnterSsid;
    _logCount = 0;
    if (_state == State::EnterSsid) g_ui.setTextEntryMode(true);
}

void EvilTwinScreen::onExit() {
    g_ui.setTextEntryMode(false);
}

void EvilTwinScreen::pushLog(const String& line) {
    if (_logCount < kLogLines) {
        _log[_logCount++] = line;
    } else {
        for (uint8_t i = 1; i < kLogLines; i++) _log[i - 1] = _log[i];
        _log[kLogLines - 1] = line;
    }
}

void EvilTwinScreen::onScanEvent(const ScanNotification& ev) {
    if (ev.source != ScanSource::EvilTwin) return;
    if (ev.type == ScanEventType::LogLine) pushLog(String(ev.text));
}

void EvilTwinScreen::onKey(UiKey key, char ch) {
    switch (_state) {
        case State::EnterSsid:
            // TAB, not a Char branch: text entry mode is active here for
            // typing the SSID, so a printable 'k'/'K' has to stay a
            // literal character (an SSID could legitimately contain one)
            // - TAB is never part of typed text, so it's free to mean
            // "start karma" instead.
            if (key == UiKey::Tab) {
                g_ui.setTextEntryMode(false);
                _logCount = 0;
                if (g_evilTwinManager.startKarma(_channel)) {
                    _state = State::Running;
                } else {
                    pushLog("no candidates - run BEACON/PROBE INTEL first");
                    g_ui.setTextEntryMode(true);
                }
            } else if (key == UiKey::Char) {
                if (_ssidText.length() < 32) _ssidText += ch;
            } else if (key == UiKey::Back) {
                if (_ssidText.length() > 0) {
                    _ssidText.remove(_ssidText.length() - 1);
                } else {
                    g_ui.popScreen();
                }
            } else if (key == UiKey::Enter && _ssidText.length() > 0) {
                g_ui.setTextEntryMode(false);
                _logCount = 0;
                if (g_evilTwinManager.start(_ssidText, _channel)) {
                    _state = State::Running;
                } else {
                    pushLog("failed to start AP");
                    g_ui.setTextEntryMode(true);
                }
            }
            break;

        case State::Running:
            if (key == UiKey::Enter || key == UiKey::Back) {
                g_evilTwinManager.stop();
                g_ui.popScreen();
            }
            break;
    }
}

void EvilTwinScreen::draw(M5Canvas& gfx) {
    gfx.fillScreen(theme::BG);
    chrome::drawHeader(gfx, "EVIL TWIN");

    switch (_state) {
        case State::EnterSsid: {
            gfx.setTextColor(theme::CYAN, theme::BG);
            gfx.setCursor(6, 20);
            gfx.print("SSID to clone:");

            gfx.fillRect(6, 32, gfx.width() - 12, 10, theme::PANEL_BG);
            gfx.setTextColor(theme::GREEN_BRIGHT, theme::PANEL_BG);
            gfx.setCursor(8, 33);
            gfx.print(_ssidText);

            gfx.setTextColor(theme::AMBER, theme::BG);
            gfx.setCursor(6, 48);
            gfx.print("Always broadcasts OPEN (no");
            gfx.setCursor(6, 58);
            gfx.print("password) regardless of the real");
            gfx.setCursor(6, 68);
            gfx.print("network's actual encryption.");

            std::vector<String> candidates;
            size_t karmaCount = g_evilTwinManager.previewKarmaCandidates(candidates);
            gfx.setTextColor(karmaCount ? theme::CYAN : theme::GREY, theme::BG);
            gfx.setCursor(6, 82);
            gfx.print("TAB: karma (");
            gfx.print((unsigned)karmaCount);
            gfx.print(karmaCount ? " candidates)" : " - none yet)");

            gfx.setTextColor(theme::GREY, theme::BG);
            gfx.setCursor(4, gfx.height() - 9);
            gfx.print("ENTER:start DEL:erase/back");
            break;
        }

        case State::Running: {
            bool karma = g_evilTwinManager.isKarmaMode();
            gfx.setTextColor(theme::RED, theme::BG);
            gfx.setCursor(6, 18);
            if (karma) {
                gfx.print("KARMA (");
                gfx.print((unsigned)(g_evilTwinManager.karmaCurrentIndex() + 1));
                gfx.print("/");
                gfx.print((unsigned)g_evilTwinManager.karmaCandidateCount());
                gfx.print("): ");
            } else {
                gfx.print("AP UP: ");
            }
            gfx.print(g_evilTwinManager.ssid());

            gfx.setTextColor(theme::CYAN, theme::BG);
            gfx.setCursor(6, 30);
            gfx.print("connections: ");
            gfx.print((unsigned)g_evilTwinManager.associationCount());

            gfx.setTextColor(theme::GREY, theme::BG);
            gfx.drawFastHLine(4, 40, gfx.width() - 8, theme::GREY);
            for (uint8_t i = 0; i < _logCount; i++) {
                int16_t y = 43 + i * 9;
                gfx.setTextColor(theme::MAGENTA, theme::BG);
                gfx.setCursor(6, y);
                String line = _log[i];
                if (line.length() > 37) line = line.substring(0, 37);
                gfx.print(line);
            }

            gfx.setTextColor(theme::GREY, theme::BG);
            gfx.setCursor(4, gfx.height() - 9);
            gfx.print("ENTER/DEL: stop AP");
            break;
        }
    }
}
