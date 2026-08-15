#include "WardrivingScreen.h"
#include "SignalFinderScreen.h"
#include "EvilTwinScreen.h"
#include "DeauthScreen.h"
#include "PmkidScreen.h"
#include "PmkidSweepScreen.h"
#include "OpenConnectScreen.h"
#include "OffensiveDisclaimerScreen.h"
#include "../UiManager.h"
#include "../Theme.h"
#include "../Chrome.h"
#include "../TextWrap.h"
#include "../../core/Config.h"
#include "../../scan/WardrivingManager.h"
#include <cstdio>

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
    // Approximate the recording clock across screen re-entry: if we come
    // back while a session is already running and haven't stamped a start
    // yet, stamp it now (the manager runs in the background, so this is a
    // UI-side timer, not the true session start).
    if (_state == State::Running && _recordStartMs == 0) _recordStartMs = millis();
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
    if (_showDetail) {
        _showDetail = false;
        return;
    }
    switch (_state) {
        case State::Idle:
            if (key == UiKey::Char && (ch == 'i' || ch == 'I')) {
                if (g_wardrivingManager.sightingCount() > 0) _showDetail = true;
            } else if (key == UiKey::Enter) {
                g_wardrivingManager.start();
                _logCount = 0;
                _recordStartMs = millis();
                _state = State::Running;
            } else if (key == UiKey::Up) {
                if (_sightingsSelected > 0) _sightingsSelected--;
            } else if (key == UiKey::Down) {
                if (_sightingsSelected + 1 < g_wardrivingManager.sightingCount()) _sightingsSelected++;
            } else if (key == UiKey::Char && (ch == 'a' || ch == 'A')) {
                _allowlistSelected = 0;
                _state = State::AllowlistView;
            } else if (key == UiKey::Tab) {
                WardrivingManager::ApSighting ap;
                if (g_wardrivingManager.getSighting(_sightingsSelected, ap)) {
                    SignalFinderScreen::instance().setTarget(ap.ssid, ap.bssid);
                    g_ui.pushScreen(&SignalFinderScreen::instance());
                }
            } else if (key == UiKey::Char && (ch == 'e' || ch == 'E')) {
                WardrivingManager::ApSighting ap;
                if (g_wardrivingManager.getSighting(_sightingsSelected, ap) && !ap.ssid.equals("<hidden>")) {
                    EvilTwinScreen::instance().setSuggestedSsid(ap.ssid, ap.channel);
                    if (g_config.offensiveEnabled) {
                        g_ui.pushScreen(&EvilTwinScreen::instance());
                    } else {
                        OffensiveDisclaimerScreen::instance().setPendingTargetScreen(&EvilTwinScreen::instance());
                        g_ui.pushScreen(&OffensiveDisclaimerScreen::instance());
                    }
                }
            } else if (key == UiKey::Char && (ch == 'x' || ch == 'X')) {
                WardrivingManager::ApSighting ap;
                if (g_wardrivingManager.getSighting(_sightingsSelected, ap)) {
                    DeauthScreen::instance().setTarget(ap.ssid, ap.bssid, ap.channel);
                    if (g_config.offensiveEnabled) {
                        g_ui.pushScreen(&DeauthScreen::instance());
                    } else {
                        OffensiveDisclaimerScreen::instance().setPendingTargetScreen(&DeauthScreen::instance());
                        g_ui.pushScreen(&OffensiveDisclaimerScreen::instance());
                    }
                }
            } else if (key == UiKey::Char && (ch == 'p' || ch == 'P')) {
                WardrivingManager::ApSighting ap;
                if (g_wardrivingManager.getSighting(_sightingsSelected, ap)) {
                    PmkidScreen::instance().setTarget(ap.ssid, ap.bssid, ap.channel);
                    if (g_config.offensiveEnabled) {
                        g_ui.pushScreen(&PmkidScreen::instance());
                    } else {
                        OffensiveDisclaimerScreen::instance().setPendingTargetScreen(&PmkidScreen::instance());
                        g_ui.pushScreen(&OffensiveDisclaimerScreen::instance());
                    }
                }
            } else if (key == UiKey::Char && (ch == 's' || ch == 'S')) {
                // Unlike E/X/P above, this doesn't need a selected
                // sighting - PmkidSweepScreen gathers every eligible
                // (non-open, non-hidden) AP itself when started.
                if (g_config.offensiveEnabled) {
                    g_ui.pushScreen(&PmkidSweepScreen::instance());
                } else {
                    OffensiveDisclaimerScreen::instance().setPendingTargetScreen(&PmkidSweepScreen::instance());
                    g_ui.pushScreen(&OffensiveDisclaimerScreen::instance());
                }
            } else if (key == UiKey::Char && (ch == 'c' || ch == 'C')) {
                // Join a selected OPEN network (no password) and check for a
                // captive portal. Only meaningful for open APs — a
                // password-protected one can't be joined from here anyway.
                WardrivingManager::ApSighting ap;
                if (g_wardrivingManager.getSighting(_sightingsSelected, ap) && ap.open &&
                    !ap.ssid.equals("<hidden>")) {
                    OpenConnectScreen::instance().setTarget(ap.ssid);
                    g_ui.pushScreen(&OpenConnectScreen::instance());
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
            } else if (key == UiKey::Char && (ch == 'i' || ch == 'I')) {
                if (g_wardrivingManager.sightingCount() > 0) _showDetail = true;
            } else if (key == UiKey::Up) {
                if (_sightingsSelected > 0) _sightingsSelected--;
            } else if (key == UiKey::Down) {
                if (_sightingsSelected + 1 < g_wardrivingManager.sightingCount()) _sightingsSelected++;
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
    if (_showDetail) {
        WardrivingManager::ApSighting ap;
        if (g_wardrivingManager.getSighting(_sightingsSelected, ap)) {
            String text = "SSID: " + ap.ssid + " / BSSID: " + ap.bssid + " / vendor: " +
                          (ap.vendor.length() ? ap.vendor : String("unknown")) +
                          (ap.suspiciousNote.length() ? (" / " + ap.suspiciousNote) : String(""));
            chrome::drawDetailOverlay(gfx, "AP SIGHTING", text);
        }
        return;
    }

    gfx.fillScreen(theme::BG);

    if (_state == State::AllowlistView || _state == State::AllowlistAddEntry || _state == State::AllowlistAddConfirm) {
        chrome::drawHeader(gfx, "WARDRIVE ALLOWLIST");
    } else {
        chrome::drawHeader(gfx, "WAR DRIVING");
    }

    switch (_state) {
        case State::Idle: {
            drawStatusStrip(gfx, /*recording=*/false);
            drawSightings(gfx, 56);
            gfx.setTextColor(theme::GREY, theme::BG);
            gfx.setCursor(4, gfx.height() - 9);
            gfx.print("TAB:loc A:al C:cn E:twn X:dth P:pmk");
            break;
        }

        case State::Running: {
            drawStatusStrip(gfx, /*recording=*/true);
            drawSightings(gfx, 56);
            gfx.setTextColor(theme::GREY, theme::BG);
            gfx.setCursor(4, gfx.height() - 9);
            gfx.print("ENTER:stop A:allowlist DEL:back(bg)");
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

void WardrivingScreen::drawStatusStrip(M5Canvas& gfx, bool recording) {
    // Status box: STATUS + session TIME.
    gfx.drawRect(4, 18, gfx.width() - 8, 13, theme::CYAN);
    gfx.setTextColor(theme::GREY, theme::BG);
    gfx.setCursor(8, 20);
    gfx.print("STATUS: ");
    gfx.setTextColor(recording ? theme::GREEN : theme::AMBER, theme::BG);
    gfx.print(recording ? "RECORDING" : "STANDBY");

    uint32_t sec = recording ? (millis() - _recordStartMs) / 1000 : 0;
    char t[12];
    snprintf(t, sizeof(t), "%02u:%02u:%02u", (unsigned)(sec / 3600), (unsigned)((sec / 60) % 60),
             (unsigned)(sec % 60));
    gfx.setTextColor(theme::GREY, theme::BG);
    gfx.setCursor(150, 20);
    gfx.print("TIME ");
    gfx.setTextColor(theme::CYAN, theme::BG);
    gfx.print(t);

    // Four real-metric stat boxes (no GPS/SPEED on this hardware — see
    // README "Limiti noti"): APs seen, open, discovered, evil-twin.
    struct Box {
        const char* label;
        uint32_t value;
        uint16_t color;
    };
    uint32_t evil = g_wardrivingManager.suspiciousCount();
    Box boxes[4] = {
        {"SEEN", (uint32_t)g_wardrivingManager.sightingCount(), theme::CYAN},
        {"OPEN", g_wardrivingManager.openCount(), theme::AMBER},
        {"DISC", g_wardrivingManager.discoveredCount(), theme::MAGENTA},
        {"EVIL", evil, evil ? theme::RED : theme::GREEN},
    };
    const int16_t bx[4] = {4, 60, 116, 172};
    for (int i = 0; i < 4; i++) {
        gfx.drawRect(bx[i], 34, 54, 20, theme::MAGENTA);
        gfx.setTextColor(theme::GREY, theme::BG);
        gfx.setCursor(bx[i] + 4, 37);
        gfx.print(boxes[i].label);
        gfx.setTextColor(boxes[i].color, theme::BG);
        gfx.setCursor(bx[i] + 4, 46);
        gfx.print((unsigned)boxes[i].value);
    }
}

void WardrivingScreen::drawSightings(M5Canvas& gfx, int16_t top) {
    size_t count = g_wardrivingManager.sightingCount();
    if (count == 0) {
        gfx.setTextColor(theme::GREY, theme::BG);
        gfx.setCursor(6, top + 6);
        gfx.print(g_wardrivingManager.isRunning() ? "scanning for APs..." : "no APs logged yet");
        return;
    }

    gfx.setTextColor(theme::GREY, theme::BG);
    gfx.drawFastHLine(4, top, gfx.width() - 8, theme::GREY);

    constexpr int16_t kRowH = 10;
    constexpr size_t kMaxRows = 6;

    size_t first = 0;
    if (_sightingsSelected >= kMaxRows) first = _sightingsSelected - kMaxRows + 1;

    WardrivingManager::ApSighting ap;
    for (size_t row = 0; row < kMaxRows; row++) {
        size_t i = first + row;
        if (i >= count) break;
        if (!g_wardrivingManager.getSighting(i, ap)) continue;

        int16_t y = top + 3 + (int16_t)row * kRowH;
        bool sel = (i == _sightingsSelected);
        uint16_t rowBg = sel ? theme::PANEL_BG : theme::BG;
        if (sel) gfx.fillRect(0, y - 1, gfx.width(), kRowH, rowBg);

        // Priority: possible evil twin outranks discovered, which outranks
        // merely-open, which outranks a normal secured AP.
        uint16_t color = sel ? theme::CYAN
                             : (ap.suspicious ? theme::RED
                                              : (ap.discovered ? theme::MAGENTA
                                                               : (ap.open ? theme::AMBER : theme::GREEN)));
        if (sel) {
            gfx.setTextColor(theme::CYAN, rowBg);
            gfx.setCursor(1, y);
            gfx.print(">");
        }
        gfx.setTextColor(color, rowBg);
        gfx.setCursor(8, y);
        String ssid = ap.ssid;
        if (ssid.length() > 15) ssid = ssid.substring(0, 15);
        gfx.print(ssid);

        gfx.setTextColor(sel ? theme::CYAN : theme::GREY, rowBg);
        gfx.setCursor(104, y);
        gfx.print(ap.channel);

        chrome::drawSignalBars(gfx, 126, y + 8, ap.rssi);

        gfx.setTextColor(sel ? theme::CYAN : chrome::securityColor(ap.encryption), rowBg);
        gfx.setCursor(150, y);
        gfx.print(chrome::securityLabel(ap.encryption));

        if (ap.suspicious) {
            gfx.setTextColor(theme::RED, rowBg);
            gfx.setCursor(212, y);
            gfx.print("!");
        }
    }

    chrome::drawScrollMarkers(gfx, top + 3, top + 3 + (int16_t)kMaxRows * kRowH, first > 0, (first + kMaxRows) < count);
}
