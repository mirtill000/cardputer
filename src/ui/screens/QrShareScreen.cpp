#include "QrShareScreen.h"
#include "../UiManager.h"
#include "../Theme.h"
#include "../Chrome.h"
#include "../../core/Types.h"
#include "../../scan/ScanManager.h"
#include "../../net/WifiManager.h"

QrShareScreen& QrShareScreen::instance() {
    static QrShareScreen s;
    return s;
}

void QrShareScreen::onEnter() {}

void QrShareScreen::onKey(UiKey key, char /*ch*/) {
    if (key == UiKey::Back) g_ui.popScreen();
}

void QrShareScreen::draw(M5Canvas& gfx) {
    gfx.fillScreen(theme::BG);
    chrome::drawHeader(gfx, "SHARE (QR)");

    // Build a compact summary (kept short so it fits a low QR version).
    uint32_t alive = 0, warn = 0, crit = 0;
    size_t n = g_scanManager.hostCount();
    HostInfo h;
    String critIps;
    for (size_t i = 0; i < n; i++) {
        if (!g_scanManager.getHost(i, h) || !h.alive) continue;
        alive++;
        if (h.risk == RiskLevel::Warning) warn++;
        if (h.risk == RiskLevel::Critical) {
            crit++;
            if (critIps.length() < 40) critIps += h.ip.toString() + " ";
        }
    }

    String summary = "NETRUNNER ";
    summary += g_wifi.isConnected() ? g_wifi.currentSsid() : String("offline");
    summary += " hosts:" + String(alive) + " W:" + String(warn) + " C:" + String(crit);
    if (critIps.length()) summary += " crit: " + critIps;
    if (summary.length() > 90) summary = summary.substring(0, 90);  // keep within a low QR version

    // QR centred; version 6 comfortably holds ~90 bytes.
    constexpr int16_t kQrW = 104;
    int16_t qx = (gfx.width() - kQrW) / 2;
    gfx.qrcode(summary.c_str(), qx, 20, kQrW, 6);

    gfx.setTextColor(theme::GREY, theme::BG);
    gfx.setCursor(4, gfx.height() - 9);
    gfx.print("scan with a phone  DEL:back");
}
