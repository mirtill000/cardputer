#include "ChannelScanScreen.h"
#include "../UiManager.h"
#include "../Theme.h"
#include "../Chrome.h"
#include "../../net/WifiManager.h"

ChannelScanScreen& ChannelScanScreen::instance() {
    static ChannelScanScreen s;
    return s;
}

void ChannelScanScreen::onEnter() {
    for (auto& c : _apCount) c = 0;
    for (auto& s : _rssiSum) s = 0;
    _selected = 0;
    _scanCount = 0;
    g_wifi.beginScan();
}

void ChannelScanScreen::onKey(UiKey key, char /*ch*/) {
    if (key == UiKey::Left) {
        if (_selected > 0) _selected--;
    } else if (key == UiKey::Right) {
        if (_selected + 1 < kChannels) _selected++;
    } else if (key == UiKey::Back) {
        g_ui.popScreen();
    }
}

void ChannelScanScreen::update(uint32_t /*nowMs*/) {
    int16_t count = g_wifi.scanStatus();
    if (count == WifiManager::kScanRunning) return;  // still waiting on the in-flight scan

    if (count >= 0) {
        for (auto& c : _apCount) c = 0;
        for (auto& s : _rssiSum) s = 0;

        WifiManager::ScanResult r;
        for (int16_t i = 0; i < count; i++) {
            if (!g_wifi.getScanResult(i, r)) continue;
            if (r.channel < 1 || r.channel > kChannels) continue;  // 2.4GHz only on this hardware - see README
            uint8_t idx = (uint8_t)(r.channel - 1);
            _apCount[idx]++;
            _rssiSum[idx] += r.rssi;
        }
        _scanCount++;
    }

    g_wifi.beginScan();  // immediately re-arm - this screen's whole point is a live reading
}

void ChannelScanScreen::draw(M5Canvas& gfx) {
    gfx.fillScreen(theme::BG);
    chrome::drawHeader(gfx, "CHANNEL SCAN");

    uint16_t totalAps = 0;
    for (uint8_t i = 0; i < kChannels; i++) totalAps += _apCount[i];

    gfx.setTextColor(theme::GREEN, theme::BG);
    gfx.setCursor(6, 18);
    gfx.print("APs seen: ");
    gfx.print(totalAps);
    if (_scanCount == 0) gfx.print("  scanning...");

    constexpr int16_t kBarTop = 30;
    constexpr int16_t kBarBottom = 96;
    constexpr int16_t kBarMaxH = kBarBottom - kBarTop;
    constexpr int16_t kSlotW = 17;
    constexpr int16_t kBarW = 13;
    constexpr int16_t kXStart = 8;
    // Bars scale against a fixed cap rather than the busiest channel THIS
    // scan happened to see - a dynamic max would make bar heights jitter
    // scan to scan even when nothing really changed, which reads worse
    // than an occasional clipped-at-full-height bar in a genuinely busy
    // area.
    constexpr float kApsForFullBar = 8.0f;

    uint8_t myChannel = g_wifi.currentChannel();

    for (uint8_t ch = 1; ch <= kChannels; ch++) {
        uint8_t idx = (uint8_t)(ch - 1);
        int16_t x = kXStart + (int16_t)idx * kSlotW;
        uint16_t count = _apCount[idx];

        bool sel = (idx == _selected);
        if (sel) gfx.drawRect(x - 1, kBarTop - 1, kBarW + 2, kBarMaxH + 2, theme::CYAN);

        if (count > 0) {
            float frac = (float)(count > 8 ? 8 : count) / kApsForFullBar;
            int16_t barH = (int16_t)(frac * (float)kBarMaxH);
            if (barH < 1) barH = 1;
            uint16_t color = (count <= 1) ? theme::GREEN : (count <= 4) ? theme::AMBER : theme::RED;
            gfx.fillRect(x, kBarBottom - barH, kBarW, barH, color);
        }

        if (ch == myChannel) {
            gfx.setTextColor(theme::CYAN, theme::BG);
            gfx.setCursor(x + 3, kBarTop - 10);
            gfx.print("v");
        }

        gfx.setTextColor(sel ? theme::CYAN : theme::GREY, theme::BG);
        gfx.setCursor(x + (ch < 10 ? 3 : 0), kBarBottom + 2);
        gfx.print(ch);
    }

    uint16_t selCount = _apCount[_selected];
    gfx.setTextColor(theme::MAGENTA, theme::BG);
    gfx.setCursor(6, kBarBottom + 12);
    gfx.print("ch ");
    gfx.print((unsigned)(_selected + 1));
    gfx.print(": ");
    gfx.print(selCount);
    gfx.print(" APs");
    if (selCount > 0) {
        int32_t avgRssi = _rssiSum[_selected] / (int32_t)selCount;
        gfx.print(", avg ");
        gfx.print(avgRssi);
        gfx.print("dBm");
    }
    if ((uint8_t)(_selected + 1) == myChannel) gfx.print(" (yours)");

    gfx.setTextColor(theme::GREY, theme::BG);
    gfx.setCursor(4, gfx.height() - 9);
    gfx.print("Left/Right:select  DEL:back");
}
