#include "WardrivingScreen.h"
#include "SignalFinderScreen.h"
#include "BleScanScreen.h"
#include "../UiManager.h"
#include "../Theme.h"
#include "../Chrome.h"
#include "../TextWrap.h"
#include "../../scan/WardrivingManager.h"

namespace {
constexpr size_t kMaxAllowlistLen = 32;
}  // namespace

WardrivingScreen& WardrivingScreen::instance() {
    static WardrivingScreen s;
    return s;
}

void WardrivingScreen::onEnter() {
    // Reflects actual manager state rather than assuming Idle - the
    // manager keeps running in the background across screen visits
    // (see onKey's Back handling in the Running state below), so
    // re-entering this screen needs to pick that back up.
    _state = g_wardrivingManager.isRunning() ? State::Running : State::Idle;
    _logCount = 0;
}

void WardrivingScreen::onExit() {
    g_ui.setTextEntryMode(false);  // redundant with UiManager's own safety net, but explicit costs nothing
}

void WardrivingScreen::pushLog(const String& line) {
    if (_logCount < kLogLines) {
        _log[_logCount++] = line;
    } else {
        for (uint8_t i = 1; i < kLogLines; i++) _log[i - 1] = _log[i];
        _log[kLogLines - 1] = line;
    }
}

void WardrivingScreen::onScanEvent(const ScanNotification& ev) {
    if (ev.source != ScanSource::Wardriving) return;  // not ours — see ScanSource in EventQueue.h
    if (ev.type == ScanEventType::LogLine) pushLog(String(ev.text));
}

void WardrivingScreen::onKey(UiKey key, char ch) {
    switch (_state) {
        case State::Idle:
            if (key == UiKey::Enter) {
                g_wardrivingManager.start();
                _logCount = 0;
                _state = State::Running;
            } else if (key == UiKey::Up) {
                if (_sightingsSelected > 0) _sightingsSelected--;
            } else if (key == UiKey::Down) {
                if (_sightingsSelected + 1 < g_wardrivingManager.sightingCount()) _sightingsSelected++;
            } else if (key == UiKey::Char && (ch == 'a' || ch == 'A')) {
                _allowlistSelected = 0;
                _state = State::AllowlistView;
            } else if (key == UiKey::Char && (ch == 'b' || ch == 'B')) {
                g_ui.pushScreen(&BleScanScreen::instance());
            } else if (key == UiKey::Tab) {
                WardrivingManager::ApSighting ap;
                if (g_wardrivingManager.getSighting(_sightingsSelected, ap)) {
                    SignalFinderScreen::instance().setTarget(ap.ssid, ap.bssid);
                    g_ui.pushScreen(&SignalFinderScreen::instance());
                }
            } else if (key == UiKey::Back) {
                g_ui.popScreen();
            }
            break;

        case State::Running:
            if (key == UiKey::Enter) {
                g_wardrivingManager.stop();
                _state = State::Idle;
            } else if (key == UiKey::Char && (ch == 'a' || ch == 'A')) {
                _allowlistSelected = 0;
                _state = State::AllowlistView;
            } else if (key == UiKey::Char && (ch == 'b' || ch == 'B')) {
                g_ui.pushScreen(&BleScanScreen::instance());
            } else if (key == UiKey::Back) {
                g_ui.popScreen();  // keeps running in the background - see WardrivingManager
            }
            break;

        case State::AllowlistView: {
            uint8_t count = g_wardrivingManager.allowlistCount();
            if (key == UiKey::Up) {
                if (_allowlistSelected > 0) _allowlistSelected--;
            } else if (key == UiKey::Down) {
                if (_allowlistSelected + 1 < count) _allowlistSelected++;
            } else if (key == UiKey::Char && (ch == 'n' || ch == 'N')) {
                _newSsid = "";
                g_ui.setTextEntryMode(true);
                _state = State::AllowlistAddEntry;
            } else if (key == UiKey::Char && (ch == 'd' || ch == 'D') && count > 0) {
                g_wardrivingManager.removeFromAllowlist((uint8_t)_allowlistSelected);
                if (_allowlistSelected > 0 && _allowlistSelected >= g_wardrivingManager.allowlistCount()) {
                    _allowlistSelected--;
                }
            } else if (key == UiKey::Back) {
                _state = g_wardrivingManager.isRunning() ? State::Running : State::Idle;
            }
            break;
        }

        case State::AllowlistAddEntry:
            if (key == UiKey::Char) {
                if (_newSsid.length() < kMaxAllowlistLen) _newSsid += ch;
            } else if (key == UiKey::Back) {
                if (_newSsid.length() > 0) {
                    _newSsid.remove(_newSsid.length() - 1);
                } else {
                    g_ui.setTextEntryMode(false);
                    _state = State::AllowlistView;
                }
            } else if (key == UiKey::Enter) {
                if (_newSsid.length() > 0) {
                    g_ui.setTextEntryMode(false);
                    _state = State::AllowlistAddConfirm;
                }
            }
            break;

        case State::AllowlistAddConfirm:
            if (key == UiKey::Char && (ch == 'y' || ch == 'Y')) {
                g_wardrivingManager.addToAllowlist(_newSsid);
                _newSsid = "";
                _state = State::AllowlistView;
            } else if (key == UiKey::Back) {
                _newSsid = "";
                _state = State::AllowlistView;
            }
            break;
    }
}

void WardrivingScreen::draw(M5Canvas& gfx) {
    gfx.fillScreen(theme::BG);

    if (_state == State::AllowlistView || _state == State::AllowlistAddEntry || _state == State::AllowlistAddConfirm) {
        chrome::drawHeader(gfx, "WARDRIVE ALLOWLIST");
    } else {
        chrome::drawHeader(gfx, "WAR DRIVING");
    }

    switch (_state) {
        case State::Idle: {
            gfx.setTextColor(theme::GREEN, theme::BG);
            gfx.setCursor(6, 18);
            gfx.print("seen:");
            gfx.print((unsigned)g_wardrivingManager.sightingCount());
            gfx.print(" open:");
            gfx.print((unsigned)g_wardrivingManager.openCount());
            gfx.print(" disc:");
            gfx.print((unsigned)g_wardrivingManager.discoveredCount());
            if (g_wardrivingManager.suspiciousCount() > 0) {
                gfx.setTextColor(theme::RED, theme::BG);
                gfx.print(" evil:");
                gfx.print((unsigned)g_wardrivingManager.suspiciousCount());
            }

            drawSightings(gfx, 30);

            gfx.setTextColor(theme::MAGENTA, theme::BG);
            gfx.setCursor(6, gfx.height() - 20);
            gfx.print("ENTER: start passive scan");

            gfx.setTextColor(theme::GREY, theme::BG);
            gfx.setCursor(4, gfx.height() - 9);
            gfx.print("TAB:locate A:allowlist B:ble DEL:back");
            break;
        }

        case State::Running: {
            gfx.setTextColor(theme::CYAN, theme::BG);
            gfx.setCursor(6, 18);
            gfx.print("seen:");
            gfx.print((unsigned)g_wardrivingManager.sightingCount());
            gfx.print(" open:");
            gfx.print((unsigned)g_wardrivingManager.openCount());
            gfx.print(" disc:");
            gfx.print((unsigned)g_wardrivingManager.discoveredCount());
            if (g_wardrivingManager.suspiciousCount() > 0) {
                gfx.setTextColor(theme::RED, theme::BG);
                gfx.print(" evil:");
                gfx.print((unsigned)g_wardrivingManager.suspiciousCount());
            }

            gfx.setTextColor(theme::GREY, theme::BG);
            gfx.drawFastHLine(4, 29, gfx.width() - 8, theme::GREY);

            for (uint8_t i = 0; i < _logCount; i++) {
                int16_t y = 32 + i * 9;
                bool isEvilTwin = _log[i].indexOf("evil twin") >= 0;
                bool isOpen = _log[i].indexOf("(OPEN)") >= 0;
                gfx.setTextColor(isEvilTwin ? theme::RED : (isOpen ? theme::AMBER : theme::GREEN), theme::BG);
                gfx.setCursor(6, y);
                String line = _log[i];
                if (line.length() > 37) line = line.substring(0, 37);
                gfx.print(line);
            }

            gfx.setTextColor(theme::GREY, theme::BG);
            gfx.setCursor(4, gfx.height() - 9);
            gfx.print("ENTER:stop A:list B:ble DEL:back(bg)");
            break;
        }

        case State::AllowlistView:
            drawAllowlist(gfx, 20);
            gfx.setTextColor(theme::GREY, theme::BG);
            gfx.setCursor(4, gfx.height() - 9);
            gfx.print(g_wardrivingManager.allowlistCount() > 0 ? "N:add D:delete DEL:back" : "N:add DEL:back");
            break;

        case State::AllowlistAddEntry: {
            gfx.setTextColor(theme::CYAN, theme::BG);
            gfx.setCursor(6, 22);
            gfx.print("SSID to authorize:");

            gfx.fillRect(6, 34, gfx.width() - 12, 10, theme::PANEL_BG);
            gfx.setTextColor(theme::GREEN_BRIGHT, theme::PANEL_BG);
            gfx.setCursor(8, 35);
            gfx.print(_newSsid);

            gfx.setTextColor(theme::GREY, theme::BG);
            gfx.setCursor(4, gfx.height() - 9);
            gfx.print("ENTER:next DEL:erase/back");
            break;
        }

        case State::AllowlistAddConfirm:
            gfx.setTextColor(theme::RED, theme::BG);
            gfx.setCursor(6, 20);
            gfx.print("CONFIRM AUTHORIZATION");

            gfx.setTextColor(theme::AMBER, theme::BG);
            drawWrapped(gfx,
                        ("Add \"" + _newSsid +
                         "\" to the allowlist? When open, this network will be auto-joined, "
                         "discovery- and port-scanned, and the results saved, with no further "
                         "confirmation. Only add networks YOU own or are explicitly authorized to test.")
                            .c_str(),
                        6, 32, 10, 37);

            gfx.setTextColor(theme::MAGENTA, theme::BG);
            gfx.setCursor(6, 110);
            gfx.print("Y: I own/am authorized");
            gfx.setTextColor(theme::GREY, theme::BG);
            gfx.setCursor(6, 122);
            gfx.print("DEL: cancel");
            break;
    }
}

void WardrivingScreen::drawAllowlist(M5Canvas& gfx, int16_t top) {
    uint8_t count = g_wardrivingManager.allowlistCount();
    if (count == 0) {
        gfx.setTextColor(theme::AMBER, theme::BG);
        gfx.setCursor(6, top + 4);
        gfx.print("no authorized networks yet");
        gfx.setCursor(6, top + 14);
        gfx.print("press N to add one");
        return;
    }

    constexpr int16_t kRowH = 10;
    for (uint8_t i = 0; i < count; i++) {
        int16_t y = top + (int16_t)i * kRowH;
        bool sel = (i == _allowlistSelected);
        uint16_t rowBg = sel ? theme::PANEL_BG : theme::BG;
        if (sel) gfx.fillRect(0, y, gfx.width(), kRowH, rowBg);

        gfx.setTextColor(sel ? theme::CYAN : theme::GREEN, rowBg);
        gfx.setCursor(6, y + 1);
        gfx.print(g_wardrivingManager.allowlistSsid(i));
    }
}

void WardrivingScreen::drawSightings(M5Canvas& gfx, int16_t top) {
    size_t count = g_wardrivingManager.sightingCount();
    if (count == 0) return;

    gfx.setTextColor(theme::GREY, theme::BG);
    gfx.drawFastHLine(4, top, gfx.width() - 8, theme::GREY);

    constexpr int16_t kRowH = 10;
    constexpr size_t kMaxRows = 8;

    size_t first = 0;
    if (_sightingsSelected >= kMaxRows) first = _sightingsSelected - kMaxRows + 1;

    WardrivingManager::ApSighting ap;
    for (size_t row = 0; row < kMaxRows; row++) {
        size_t i = first + row;
        if (i >= count) break;
        if (!g_wardrivingManager.getSighting(i, ap)) continue;

        int16_t y = top + 2 + (int16_t)row * kRowH;
        bool sel = (i == _sightingsSelected);
        uint16_t rowBg = sel ? theme::PANEL_BG : theme::BG;
        if (sel) gfx.fillRect(0, y, gfx.width(), kRowH, rowBg);

        // Priority: possible evil twin outranks everything else here -
        // it's a stronger, more specific claim than "discovered" or
        // merely "open".
        uint16_t color = sel ? theme::CYAN
                              : (ap.suspicious ? theme::RED
                                                : (ap.discovered ? theme::MAGENTA
                                                                  : (ap.open ? theme::AMBER : theme::GREEN)));
        gfx.setTextColor(color, rowBg);
        gfx.setCursor(6, y);

        String ssid = ap.ssid;
        if (ssid.length() > 20) ssid = ssid.substring(0, 20);
        gfx.print(ssid);

        gfx.setCursor(160, y);
        gfx.print(ap.rssi);

        gfx.setCursor(190, y);
        gfx.print(ap.suspicious ? "!EVIL" : (ap.open ? "OPEN" : ""));
    }
}
