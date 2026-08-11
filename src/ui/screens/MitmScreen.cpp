#include "MitmScreen.h"
#include "../UiManager.h"
#include "../Theme.h"
#include "../Chrome.h"
#include "../TextWrap.h"
#include "../../scan/ArpSpoofManager.h"

MitmScreen& MitmScreen::instance() {
    static MitmScreen s;
    return s;
}

void MitmScreen::onEnter() {
    _state = g_arpSpoofManager.isRunning() ? State::Running : State::Idle;
    _logCount = 0;
}

void MitmScreen::onExit() {
    g_ui.setTextEntryMode(false);
}

void MitmScreen::pushLog(const String& line) {
    if (_logCount < kLogLines) {
        _log[_logCount++] = line;
    } else {
        for (uint8_t i = 1; i < kLogLines; i++) _log[i - 1] = _log[i];
        _log[kLogLines - 1] = line;
    }
}

void MitmScreen::onScanEvent(const ScanNotification& ev) {
    if (ev.source != ScanSource::ArpSpoof) return;
    if (ev.type == ScanEventType::LogLine) pushLog(String(ev.text));
    if (ev.type == ScanEventType::ScanFinished) _state = State::Idle;
}

void MitmScreen::onKey(UiKey key, char ch) {
    switch (_state) {
        case State::Idle:
            if (key == UiKey::Enter) {
                _logCount = 0;
                if (g_arpSpoofManager.start(_target, _durationS, _sniffTraffic)) {
                    _state = State::Running;
                } else {
                    pushLog("start failed - target MAC unknown? run NETWORK SCAN");
                }
            } else if (key == UiKey::Left) {
                if (_durationS > 30) _durationS -= 30;
            } else if (key == UiKey::Right) {
                if (_durationS < ArpSpoofManager::kMaxDurationS) _durationS += 30;
            } else if (key == UiKey::Char && (ch == 's' || ch == 'S')) {
                _sniffTraffic = !_sniffTraffic;
            } else if (key == UiKey::Char && (ch == 'd' || ch == 'D')) {
                _pendingHost = "";
                g_ui.setTextEntryMode(true);
                _state = State::DnsAddHost;
            } else if (key == UiKey::Back) {
                g_ui.popScreen();
            }
            break;

        case State::Running:
            if (key == UiKey::Back || key == UiKey::Enter) {
                g_arpSpoofManager.stop();
                // Stay on this screen (rather than popping) until the
                // manager confirms it actually restored the target's
                // ARP cache - see onScanEvent()'s ScanFinished handling.
            }
            break;

        case State::DnsAddHost:
            if (key == UiKey::Char) {
                if (_pendingHost.length() < 32) _pendingHost += ch;
            } else if (key == UiKey::Back) {
                if (_pendingHost.length() > 0) {
                    _pendingHost.remove(_pendingHost.length() - 1);
                } else {
                    g_ui.setTextEntryMode(false);
                    _state = State::Idle;
                }
            } else if (key == UiKey::Enter && _pendingHost.length() > 0) {
                _pendingIpText = "";
                _state = State::DnsAddIp;
            }
            break;

        case State::DnsAddIp:
            if (key == UiKey::Char) {
                if (_pendingIpText.length() < 15) _pendingIpText += ch;
            } else if (key == UiKey::Back) {
                if (_pendingIpText.length() > 0) {
                    _pendingIpText.remove(_pendingIpText.length() - 1);
                } else {
                    _state = State::DnsAddHost;
                }
            } else if (key == UiKey::Enter) {
                IPAddress ip;
                if (ip.fromString(_pendingIpText)) {
                    DnsSpoofList::add(_pendingHost, ip);
                }
                g_ui.setTextEntryMode(false);
                _state = State::Idle;
            }
            break;
    }
}

void MitmScreen::draw(M5Canvas& gfx) {
    gfx.fillScreen(theme::BG);

    if (_state == State::Running) {
        // Deliberately not chrome::drawHeader here - this needs to be
        // unmistakable, not blend in with every other screen's header.
        gfx.fillRect(0, 0, gfx.width(), 12, theme::RED);
        gfx.setTextColor(theme::BG, theme::RED);
        gfx.setCursor(4, 2);
        gfx.print("MITM ACTIVE - ");
        gfx.print(_target.toString());
    } else {
        chrome::drawHeader(gfx, "MITM AUDIT");
    }

    switch (_state) {
        case State::Idle: {
            gfx.setTextColor(theme::GREEN, theme::BG);
            gfx.setCursor(6, 18);
            gfx.print("target: ");
            gfx.print(_target.toString());

            gfx.setCursor(6, 30);
            gfx.print("duration: < ");
            gfx.print(_durationS);
            gfx.print("s >");

            gfx.setCursor(6, 42);
            gfx.print("sniff traffic (S): ");
            gfx.setTextColor(_sniffTraffic ? theme::CYAN : theme::GREY, theme::BG);
            gfx.print(_sniffTraffic ? "ON" : "OFF");

            gfx.setTextColor(theme::GREY, theme::BG);
            gfx.setCursor(6, 54);
            gfx.print("dns spoof entries: ");
            gfx.print((unsigned)DnsSpoofList::count());
            gfx.print("/");
            gfx.print((unsigned)DnsSpoofList::kMaxEntries);

            gfx.setTextColor(theme::AMBER, theme::BG);
            drawWrapped(gfx, "One-directional: poisons only the target's cache. No packet relay - see README for what this can/can't see.",
                        6, 68, 9, 37);

            gfx.setTextColor(theme::MAGENTA, theme::BG);
            gfx.setCursor(6, gfx.height() - 20);
            gfx.print("ENTER: start session");
            gfx.setTextColor(theme::GREY, theme::BG);
            gfx.setCursor(4, gfx.height() - 9);
            gfx.print("</>:duration D:dns-spoof DEL:back");
            break;
        }

        case State::Running: {
            gfx.setTextColor(theme::CYAN, theme::BG);
            gfx.setCursor(6, 16);
            gfx.print("poisoned: ");
            gfx.print((unsigned)g_arpSpoofManager.poisonPacketsSent());
            gfx.print("  left: ");
            gfx.print((unsigned)g_arpSpoofManager.secondsRemaining());
            gfx.print("s");

            gfx.setTextColor(theme::GREY, theme::BG);
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
            gfx.print("ENTER/DEL: stop (restores ARP)");
            break;
        }

        case State::DnsAddHost:
        case State::DnsAddIp: {
            gfx.setTextColor(theme::CYAN, theme::BG);
            gfx.setCursor(6, 22);
            gfx.print(_state == State::DnsAddHost ? "hostname to spoof:" : "answer with IP:");

            gfx.fillRect(6, 34, gfx.width() - 12, 10, theme::PANEL_BG);
            gfx.setTextColor(theme::GREEN_BRIGHT, theme::PANEL_BG);
            gfx.setCursor(8, 35);
            gfx.print(_state == State::DnsAddHost ? _pendingHost : _pendingIpText);

            if (_state == State::DnsAddIp) {
                gfx.setTextColor(theme::GREY, theme::BG);
                gfx.setCursor(6, 50);
                gfx.print("for: ");
                gfx.print(_pendingHost);
            }

            gfx.setTextColor(theme::GREY, theme::BG);
            gfx.setCursor(4, gfx.height() - 9);
            gfx.print("ENTER:next DEL:erase/back");
            break;
        }
    }
}
