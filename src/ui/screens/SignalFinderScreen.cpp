#include "SignalFinderScreen.h"
#include "../UiManager.h"
#include "../Theme.h"
#include "../Chrome.h"
#include "../../net/WifiManager.h"

SignalFinderScreen& SignalFinderScreen::instance() {
    static SignalFinderScreen s;
    return s;
}

void SignalFinderScreen::setTarget(const String& ssid, const String& bssid) {
    _ssid = ssid;
    _bssid = bssid;
}

void SignalFinderScreen::onEnter() {
    _found = false;
    _havePrev = false;
    _rssi = 0;
    _prevRssi = 0;
    _scanCount = 0;
    g_wifi.beginScan();
}

void SignalFinderScreen::onKey(UiKey key, char /*ch*/) {
    if (key == UiKey::Back) g_ui.popScreen();
}

void SignalFinderScreen::update(uint32_t /*nowMs*/) {
    int16_t count = g_wifi.scanStatus();
    if (count == WifiManager::kScanRunning) return;  // still waiting on the in-flight scan

    bool found = false;
    if (count > 0) {
        WifiManager::ScanResult r;
        for (int16_t i = 0; i < count; i++) {
            if (g_wifi.getScanResult(i, r) && r.bssid == _bssid) {
                _prevRssi = _havePrev ? _rssi : r.rssi;
                _rssi = r.rssi;
                _havePrev = true;
                found = true;
                break;
            }
        }
    }
    _found = found;
    _scanCount++;

    g_wifi.beginScan();  // immediately re-arm - this screen's whole point is a continuous live reading
}

void SignalFinderScreen::draw(M5Canvas& gfx) {
    gfx.fillScreen(theme::BG);
    chrome::drawHeader(gfx, "SIGNAL FINDER");

    gfx.setTextColor(theme::GREEN, theme::BG);
    gfx.setCursor(6, 18);
    String ssid = _ssid.length() ? _ssid : String("<hidden>");
    if (ssid.length() > 34) ssid = ssid.substring(0, 34);
    gfx.print(ssid);

    gfx.setTextColor(theme::GREY, theme::BG);
    gfx.setCursor(6, 28);
    gfx.print(_bssid);

    if (!_found) {
        gfx.setTextColor(theme::AMBER, theme::BG);
        gfx.setCursor(6, 55);
        gfx.print(_scanCount == 0 ? "scanning..." : "not seen this scan - keep trying");
    } else {
        constexpr int16_t kBarX = 10, kBarY = 50, kBarW = 220, kBarH = 22;
        gfx.drawRect(kBarX, kBarY, kBarW, kBarH, theme::GREY);

        // -30dBm (very close/excellent) .. -90dBm (very weak/far),
        // clamped and mapped to a 0-1 fill fraction - typical real-
        // world RSSI bounds, not a precise distance measurement.
        int32_t clamped = _rssi;
        if (clamped < -90) clamped = -90;
        if (clamped > -30) clamped = -30;
        float frac = (float)(clamped + 90) / 60.0f;
        int16_t fillW = (int16_t)(frac * (float)(kBarW - 4));
        uint16_t barColor = (frac > 0.66f) ? theme::GREEN : (frac > 0.33f ? theme::AMBER : theme::RED);
        if (fillW > 0) gfx.fillRect(kBarX + 2, kBarY + 2, fillW, kBarH - 4, barColor);

        gfx.setTextColor(theme::CYAN, theme::BG);
        gfx.setCursor(6, kBarY + kBarH + 8);
        gfx.print(_rssi);
        gfx.print(" dBm");

        if (_havePrev && _prevRssi != _rssi) {
            gfx.setTextColor(theme::MAGENTA, theme::BG);
            gfx.setCursor(80, kBarY + kBarH + 8);
            gfx.print(_rssi > _prevRssi ? "GETTING CLOSER" : "GETTING FARTHER");
        }
    }

    gfx.setTextColor(theme::GREY, theme::BG);
    gfx.setCursor(4, gfx.height() - 9);
    gfx.print("DEL:back");
}
