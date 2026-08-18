#include "VlanHopScreen.h"
#include "OffensiveDisclaimerScreen.h"
#include "../UiManager.h"
#include "../Theme.h"
#include "../Chrome.h"
#include "../../core/Config.h"
#include "../../core/Types.h"
#include "../../scan/VlanHopProbe.h"

VlanHopScreen& VlanHopScreen::instance() {
    static VlanHopScreen s;
    return s;
}

void VlanHopScreen::onEnter() {
    _running = g_vlanHopProbe.isRunning();
    _showDetail = false;
    _logCount = 0;
}

void VlanHopScreen::onScanEvent(const ScanNotification& ev) {
    if (ev.source != ScanSource::VlanHop) return;
    if (ev.type != ScanEventType::LogLine) return;
    if (_logCount < 3) {
        _log[_logCount++] = String(ev.text);
    } else {
        _log[0] = _log[1];
        _log[1] = _log[2];
        _log[2] = String(ev.text);
    }
}

void VlanHopScreen::onKey(UiKey key, char ch) {
    if (_showDetail) {
        _showDetail = false;
        return;
    }

    if (key == UiKey::Char && (ch == 'i' || ch == 'I')) {
        if (g_vlanHopProbe.sightingCount() > 0) _showDetail = true;
        return;
    }

    if (key == UiKey::Char && (ch == 'p' || ch == 'P')) {
        if (g_config.offensiveEnabled) {
            g_vlanHopProbe.sendDoubleTagProbe(_nativeVlanId, _targetVlanId);
        } else {
            // replaceScreen (not pushScreen) here: this screen is
            // already the top of the stack when 'P' is pressed, so
            // pushing the disclaimer and having it replaceScreen back
            // to us on acceptance would leave TWO stack entries for
            // this same screen. Swapping ourselves out for the
            // disclaimer up front avoids that - see
            // OffensiveDisclaimerScreen::onKey's accept path.
            OffensiveDisclaimerScreen::instance().setPendingTargetScreen(&VlanHopScreen::instance());
            g_ui.replaceScreen(&OffensiveDisclaimerScreen::instance());
        }
        return;
    }

    switch (key) {
        case UiKey::Enter:
            if (_running) {
                g_vlanHopProbe.stop();
            } else {
                g_vlanHopProbe.start();
            }
            _running = !_running;
            break;
        case UiKey::Tab:
            _nativeFieldFocused = !_nativeFieldFocused;
            break;
        case UiKey::Left:
            if (_nativeFieldFocused) {
                if (_nativeVlanId > 1) _nativeVlanId--;
            } else if (_targetVlanId > 1) {
                _targetVlanId--;
            }
            break;
        case UiKey::Right:
            if (_nativeFieldFocused) {
                if (_nativeVlanId < 4094) _nativeVlanId++;
            } else if (_targetVlanId < 4094) {
                _targetVlanId++;
            }
            break;
        case UiKey::Up:
            if (_selected > 0) _selected--;
            break;
        case UiKey::Down:
            if (_selected + 1 < g_vlanHopProbe.sightingCount()) _selected++;
            break;
        case UiKey::Back:
            g_ui.popScreen();  // keeps the passive listener running in the background
            break;
        default:
            break;
    }
}

void VlanHopScreen::draw(M5Canvas& gfx) {
    if (_showDetail) {
        VlanHopProbe::TagSighting s;
        if (g_vlanHopProbe.getSighting(_selected, s)) {
            String text = "MAC: " + macToString(s.mac) + " / outer VLAN: " + String(s.outerVlanId) + " / " +
                          (s.doubleTagged ? ("double-tagged, inner VLAN: " + String(s.innerVlanId))
                                          : String("single tag")) +
                          " / seen " + String(s.count) + "x";
            chrome::drawDetailOverlay(gfx, "VLAN TAG LEAK DETAIL", text);
        }
        return;
    }

    gfx.fillScreen(theme::BG);
    chrome::drawHeader(gfx, "VLAN HOP");

    gfx.setTextColor(_running ? theme::CYAN : theme::GREEN, theme::BG);
    gfx.setCursor(6, 18);
    gfx.print("leaks: ");
    gfx.print((unsigned)g_vlanHopProbe.sightingCount());
    gfx.print(_running ? "  [listening]" : "");

    gfx.setTextColor(_nativeFieldFocused ? theme::CYAN : theme::GREY, theme::BG);
    gfx.setCursor(6, 30);
    gfx.print("native: <");
    gfx.print(_nativeVlanId);
    gfx.print(">");

    gfx.setTextColor(!_nativeFieldFocused ? theme::CYAN : theme::GREY, theme::BG);
    gfx.setCursor(90, 30);
    gfx.print("target: <");
    gfx.print(_targetVlanId);
    gfx.print(">");

    drawSightings(gfx, 42);

    for (uint8_t i = 0; i < _logCount; i++) {
        int16_t y = (int16_t)(93 + i * 9);
        gfx.setTextColor(theme::GREY, theme::BG);
        gfx.setCursor(6, y);
        String line = _log[i];
        if (line.length() > 37) line = line.substring(0, 37);
        gfx.print(line);
    }

    gfx.setTextColor(theme::MAGENTA, theme::BG);
    gfx.setCursor(6, gfx.height() - 20);
    gfx.print(_running ? "ENTER:stop  P:send double-tag" : "ENTER:start listen  P:send probe");

    gfx.setTextColor(theme::GREY, theme::BG);
    gfx.setCursor(4, gfx.height() - 9);
    gfx.print("TAB/</>:VLAN  I:detail  DEL:back");
}

void VlanHopScreen::drawSightings(M5Canvas& gfx, int16_t top) {
    size_t count = g_vlanHopProbe.sightingCount();
    if (count == 0) {
        chrome::drawEmptyState(gfx, "no VLAN tags seen yet", _running ? "listening..." : "press ENTER to start");
        return;
    }

    gfx.setTextColor(theme::GREY, theme::BG);
    gfx.drawFastHLine(4, top, gfx.width() - 8, theme::GREY);

    constexpr int16_t kRowH = 10;
    constexpr size_t kMaxRows = 5;

    size_t first = 0;
    if (_selected >= kMaxRows) first = _selected - kMaxRows + 1;

    VlanHopProbe::TagSighting s;
    for (size_t row = 0; row < kMaxRows; row++) {
        size_t i = first + row;
        if (i >= count) break;
        if (!g_vlanHopProbe.getSighting(i, s)) continue;

        int16_t y = top + 2 + (int16_t)row * kRowH;
        bool sel = (i == _selected);
        uint16_t rowBg = sel ? theme::PANEL_BG : theme::BG;
        if (sel) gfx.fillRect(0, y, gfx.width(), kRowH, rowBg);

        gfx.setTextColor(sel ? theme::CYAN : theme::GREEN, rowBg);
        gfx.setCursor(6, y);
        gfx.print(macToString(s.mac));

        gfx.setTextColor(sel ? theme::CYAN : theme::AMBER, rowBg);
        gfx.setCursor(110, y);
        gfx.print("vlan");
        gfx.print(s.outerVlanId);

        if (s.doubleTagged) {
            gfx.setTextColor(sel ? theme::CYAN : theme::RED, rowBg);
            gfx.setCursor(190, y);
            gfx.print("2x");
        }
    }

    chrome::drawScrollMarkers(gfx, top + 2, top + 2 + (int16_t)kMaxRows * kRowH, first > 0, (first + kMaxRows) < count);
}
