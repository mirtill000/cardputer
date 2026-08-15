#include "BeaconProbeScreen.h"
#include "../UiManager.h"
#include "../Theme.h"
#include "../Chrome.h"
#include "../../scan/BeaconProbeSniffer.h"

BeaconProbeScreen& BeaconProbeScreen::instance() {
    static BeaconProbeScreen s;
    return s;
}

void BeaconProbeScreen::onEnter() {
    _running = g_beaconProbeSniffer.isRunning();
    _logCount = 0;
}

void BeaconProbeScreen::pushLog(const String& line) {
    if (_logCount < kLogLines) {
        _log[_logCount++] = line;
    } else {
        for (uint8_t i = 1; i < kLogLines; i++) _log[i - 1] = _log[i];
        _log[kLogLines - 1] = line;
    }
}

void BeaconProbeScreen::onScanEvent(const ScanNotification& ev) {
    if (ev.source != ScanSource::BeaconProbe) return;
    if (ev.type == ScanEventType::LogLine) pushLog(String(ev.text));
}

void BeaconProbeScreen::onKey(UiKey key, char ch) {
    if (_showDetail) {
        _showDetail = false;
        return;
    }
    if (key == UiKey::Char && (ch == 'i' || ch == 'I')) {
        size_t count = (_view == View::Aps) ? g_beaconProbeSniffer.apCount() : g_beaconProbeSniffer.clientCount();
        if (count > 0) _showDetail = true;
        return;
    }
    switch (key) {
        case UiKey::Enter:
            if (_running) {
                g_beaconProbeSniffer.stop();
            } else {
                g_beaconProbeSniffer.start();
            }
            _running = !_running;
            break;
        case UiKey::Tab:
            _view = (_view == View::Aps) ? View::Clients : View::Aps;
            break;
        case UiKey::Up:
            if (_view == View::Aps) {
                if (_apSelected > 0) _apSelected--;
            } else if (_clientSelected > 0) {
                _clientSelected--;
            }
            break;
        case UiKey::Down:
            if (_view == View::Aps) {
                if (_apSelected + 1 < g_beaconProbeSniffer.apCount()) _apSelected++;
            } else if (_clientSelected + 1 < g_beaconProbeSniffer.clientCount()) {
                _clientSelected++;
            }
            break;
        case UiKey::Back:
            g_ui.popScreen();  // keeps running (and hopping channels) in the background
            break;
        default:
            break;
    }
}

void BeaconProbeScreen::draw(M5Canvas& gfx) {
    if (_showDetail) {
        if (_view == View::Aps) {
            BeaconProbeSniffer::ApBeacon a;
            if (g_beaconProbeSniffer.getAp(_apSelected, a)) {
                String text = "SSID: " + (a.hidden ? String("<hidden>") : a.ssid) + " / BSSID: " + a.bssid +
                              " / vendor: " + (a.vendor.length() ? a.vendor : String("unknown"));
                chrome::drawDetailOverlay(gfx, "AP DETAIL", text);
            }
        } else {
            BeaconProbeSniffer::ProbeClient c;
            if (g_beaconProbeSniffer.getClient(_clientSelected, c)) {
                String ssids = "-";
                for (size_t i = 0; i < c.probedSsids.size(); i++) {
                    ssids = (i == 0) ? c.probedSsids[i] : (ssids + ", " + c.probedSsids[i]);
                }
                String text = "MAC: " + c.mac + (c.macRandomized ? " (randomized)" : "") + " / vendor: " +
                              (c.vendor.length() ? c.vendor : String("unknown")) + " / probed SSIDs: " + ssids;
                chrome::drawDetailOverlay(gfx, "CLIENT DETAIL", text);
            }
        }
        return;
    }

    gfx.fillScreen(theme::BG);
    chrome::drawHeader(gfx, "BEACON/PROBE");

    gfx.setTextColor(_running ? theme::CYAN : theme::GREEN, theme::BG);
    gfx.setCursor(6, 18);
    if (_view == View::Aps) {
        gfx.print("APs: ");
        gfx.print((unsigned)g_beaconProbeSniffer.apCount());
    } else {
        gfx.print("clients: ");
        gfx.print((unsigned)g_beaconProbeSniffer.clientCount());
    }
    if (_running) {
        gfx.print("  ch");
        gfx.print(g_beaconProbeSniffer.currentChannel());
    }

    gfx.setTextColor(theme::AMBER, theme::BG);
    gfx.setCursor(160, 18);
    gfx.print(_view == View::Aps ? "[AP] clients>" : "<AP [CLIENTS]");

    if (_view == View::Aps) {
        drawAps(gfx, 28);
    } else {
        drawClients(gfx, 28);
    }

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
    gfx.print(_running ? "ENTER: stop" : "ENTER: start (hops channels)");

    gfx.setTextColor(theme::GREY, theme::BG);
    gfx.setCursor(4, gfx.height() - 9);
    gfx.print("TAB:view  I:detail  DEL:back");
}

void BeaconProbeScreen::drawAps(M5Canvas& gfx, int16_t top) {
    size_t count = g_beaconProbeSniffer.apCount();
    if (count == 0) {
        chrome::drawEmptyState(gfx, "no APs seen yet", _running ? "listening..." : "press ENTER to start");
        return;
    }

    gfx.drawFastHLine(4, top, gfx.width() - 8, theme::GREY);

    constexpr int16_t kRowH = 10;
    constexpr size_t kMaxRows = 6;
    size_t first = 0;
    if (_apSelected >= kMaxRows) first = _apSelected - kMaxRows + 1;

    BeaconProbeSniffer::ApBeacon a;
    for (size_t row = 0; row < kMaxRows; row++) {
        size_t i = first + row;
        if (i >= count) break;
        if (!g_beaconProbeSniffer.getAp(i, a)) continue;

        int16_t y = top + 2 + (int16_t)row * kRowH;
        bool sel = (i == _apSelected);
        uint16_t rowBg = sel ? theme::PANEL_BG : theme::BG;
        if (sel) gfx.fillRect(0, y, gfx.width(), kRowH, rowBg);

        uint16_t nameColor = a.hiddenRevealed ? theme::MAGENTA : (sel ? theme::CYAN : theme::GREEN);
        gfx.setTextColor(nameColor, rowBg);
        gfx.setCursor(6, y);
        String label = a.hidden ? String("<hidden>") : a.ssid;
        if (label.length() > 17) label = label.substring(0, 17);
        gfx.print(label);

        gfx.setTextColor(sel ? theme::CYAN : chrome::securityColor(a.encryption), rowBg);
        gfx.setCursor(140, y);
        gfx.print(chrome::securityLabel(a.encryption));

        gfx.setTextColor(sel ? theme::CYAN : theme::GREY, rowBg);
        gfx.setCursor(200, y);
        gfx.print("ch");
        gfx.print(a.channel);
    }

    chrome::drawScrollMarkers(gfx, top + 2, top + 2 + (int16_t)kMaxRows * kRowH, first > 0, (first + kMaxRows) < count);
}

void BeaconProbeScreen::drawClients(M5Canvas& gfx, int16_t top) {
    size_t count = g_beaconProbeSniffer.clientCount();
    if (count == 0) {
        chrome::drawEmptyState(gfx, "no clients seen yet", _running ? "listening..." : "press ENTER to start");
        return;
    }

    gfx.drawFastHLine(4, top, gfx.width() - 8, theme::GREY);

    constexpr int16_t kRowH = 10;
    constexpr size_t kMaxRows = 6;
    size_t first = 0;
    if (_clientSelected >= kMaxRows) first = _clientSelected - kMaxRows + 1;

    BeaconProbeSniffer::ProbeClient c;
    for (size_t row = 0; row < kMaxRows; row++) {
        size_t i = first + row;
        if (i >= count) break;
        if (!g_beaconProbeSniffer.getClient(i, c)) continue;

        int16_t y = top + 2 + (int16_t)row * kRowH;
        bool sel = (i == _clientSelected);
        uint16_t rowBg = sel ? theme::PANEL_BG : theme::BG;
        if (sel) gfx.fillRect(0, y, gfx.width(), kRowH, rowBg);

        gfx.setTextColor(sel ? theme::CYAN : (c.macRandomized ? theme::GREY : theme::GREEN), rowBg);
        gfx.setCursor(6, y);
        gfx.print(c.mac);
        if (c.macRandomized) gfx.print(" R");

        gfx.setTextColor(sel ? theme::CYAN : theme::AMBER, rowBg);
        gfx.setCursor(150, y);
        if (c.probedSsids.empty()) {
            gfx.print("wildcard only");
        } else {
            String s = c.probedSsids[0];
            if (c.probedSsids.size() > 1) s += "+" + String((unsigned)(c.probedSsids.size() - 1));
            if (s.length() > 14) s = s.substring(0, 14);
            gfx.print(s);
        }
    }

    chrome::drawScrollMarkers(gfx, top + 2, top + 2 + (int16_t)kMaxRows * kRowH, first > 0, (first + kMaxRows) < count);
}
