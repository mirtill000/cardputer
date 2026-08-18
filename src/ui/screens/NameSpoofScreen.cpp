#include "NameSpoofScreen.h"
#include "../UiManager.h"
#include "../Theme.h"
#include "../Chrome.h"
#include "../TextWrap.h"
#include "../../scan/NameSpoofManager.h"

NameSpoofScreen& NameSpoofScreen::instance() {
    static NameSpoofScreen s;
    return s;
}

void NameSpoofScreen::onEnter() {
    _state = g_nameSpoofManager.isRunning() ? State::Running : State::Idle;
    _logCount = 0;
}

void NameSpoofScreen::pushLog(const String& line) {
    if (_logCount < kLogLines) {
        _log[_logCount++] = line;
    } else {
        for (uint8_t i = 1; i < kLogLines; i++) _log[i - 1] = _log[i];
        _log[kLogLines - 1] = line;
    }
}

void NameSpoofScreen::onScanEvent(const ScanNotification& ev) {
    if (ev.source != ScanSource::NameSpoof) return;
    if (ev.type == ScanEventType::LogLine) pushLog(String(ev.text));
    if (ev.type == ScanEventType::ScanFinished) _state = State::Idle;
}

void NameSpoofScreen::onKey(UiKey key, char ch) {
    switch (_state) {
        case State::Idle:
            if (key == UiKey::Enter) {
                _logCount = 0;
                if (g_nameSpoofManager.start(_durationS)) {
                    _state = State::Running;
                } else {
                    pushLog("start failed - already running?");
                }
            } else if (key == UiKey::Left) {
                if (_durationS > 30) _durationS -= 30;
            } else if (key == UiKey::Right) {
                if (_durationS < NameSpoofManager::kMaxDurationS) _durationS += 30;
            } else if (key == UiKey::Back) {
                g_ui.popScreen();
            }
            break;

        case State::Running:
            if (key == UiKey::Back || key == UiKey::Enter) {
                g_nameSpoofManager.stop();
                // Stay on this screen until the manager confirms the
                // sockets actually closed - see onScanEvent()'s
                // ScanFinished handling. Same pattern as MITM AUDIT.
            }
            break;
    }
}

void NameSpoofScreen::draw(M5Canvas& gfx) {
    gfx.fillScreen(theme::BG);

    if (_state == State::Running) {
        // Deliberately not chrome::drawHeader - a live LAN-wide poison
        // session needs to be unmistakable, same reasoning as MITM
        // AUDIT's red banner.
        gfx.fillRect(0, 0, gfx.width(), 12, theme::RED);
        gfx.setTextColor(theme::BG, theme::RED);
        gfx.setCursor(4, 2);
        gfx.print("NAME SPOOF ACTIVE - answering all");
    } else {
        chrome::drawHeader(gfx, "NAME SPOOF");
    }

    switch (_state) {
        case State::Idle: {
            gfx.setTextColor(theme::GREEN, theme::BG);
            gfx.setCursor(6, 20);
            gfx.print("duration: < ");
            gfx.print(_durationS);
            gfx.print("s >");

            gfx.setTextColor(theme::AMBER, theme::BG);
            drawWrapped(gfx,
                        "Answers EVERY LLMNR/NBT-NS name query on "
                        "the LAN claiming this device's IP. Any host "
                        "still relying on that fallback resolution "
                        "will connect to YOU instead. No credential "
                        "capture in this build - see README.",
                        6, 36, 9, 37);

            gfx.setTextColor(theme::MAGENTA, theme::BG);
            gfx.setCursor(6, gfx.height() - 20);
            gfx.print("ENTER: start session");
            gfx.setTextColor(theme::GREY, theme::BG);
            gfx.setCursor(4, gfx.height() - 9);
            gfx.print("</>:duration  DEL:back");
            break;
        }

        case State::Running: {
            gfx.setTextColor(theme::CYAN, theme::BG);
            gfx.setCursor(6, 16);
            gfx.print("poisoned: ");
            gfx.print((unsigned)g_nameSpoofManager.poisonedCount());
            gfx.print("  left: ");
            gfx.print((unsigned)g_nameSpoofManager.secondsRemaining());
            gfx.print("s");

            gfx.drawFastHLine(4, 27, gfx.width() - 8, theme::GREY);

            for (uint8_t i = 0; i < _logCount; i++) {
                int16_t y = 30 + i * 9;
                gfx.setTextColor(theme::GREEN, theme::BG);
                gfx.setCursor(6, y);
                String line = _log[i];
                if (line.length() > 37) line = line.substring(0, 37);
                gfx.print(line);
            }

            gfx.setTextColor(theme::GREY, theme::BG);
            gfx.setCursor(4, gfx.height() - 9);
            gfx.print("ENTER/DEL: stop session");
            break;
        }
    }
}
