#include "HostDetailScreen.h"
#include "../UiManager.h"
#include "../Theme.h"
#include "../../core/Types.h"
#include "../../scan/ScanManager.h"

HostDetailScreen& HostDetailScreen::instance() {
    static HostDetailScreen s;
    return s;
}

void HostDetailScreen::onKey(UiKey key, char /*ch*/) {
    if (key == UiKey::Back || key == UiKey::Enter) {
        g_ui.popScreen();
    }
}

namespace {
void row(M5Canvas& gfx, int16_t y, const char* label, const String& value, uint16_t valueColor) {
    gfx.setTextColor(theme::GREY, theme::BG);
    gfx.setCursor(6, y);
    gfx.print(label);
    gfx.setTextColor(valueColor, theme::BG);
    gfx.setCursor(6 + 11 * theme::GLYPH_W, y);
    gfx.print(value);
}
}  // namespace

void HostDetailScreen::draw(M5Canvas& gfx) {
    gfx.fillScreen(theme::BG);

    HostInfo h;
    if (!g_scanManager.getHost(_hostIndex, h)) {
        gfx.setTextColor(theme::RED, theme::BG);
        gfx.setCursor(6, 20);
        gfx.print("host no longer available");
        gfx.setTextColor(theme::GREY, theme::BG);
        gfx.setCursor(4, gfx.height() - 9);
        gfx.print("DEL:back");
        return;
    }

    gfx.setTextColor(theme::GREEN_BRIGHT, theme::BG);
    gfx.setCursor(4, 4);
    gfx.print(">> HOST DETAIL");
    gfx.drawFastHLine(4, 15, gfx.width() - 8, theme::GREY);

    row(gfx, 20, "IP:", h.ip.toString(), theme::GREEN);
    row(gfx, 30, "MAC:", h.macKnown ? macToString(h.mac) : String("unknown"), theme::GREEN);
    row(gfx, 40, "HOST:", h.hostname.length() ? h.hostname : String("-"), theme::GREEN);
    row(gfx, 50, "VENDOR:", h.vendor.length() ? h.vendor : String("unknown"), theme::GREEN);
    row(gfx, 60, "CLASS:", String(deviceClassLabel(h.deviceClass)), theme::CYAN);
    row(gfx, 70, "RISK:", h.risk == RiskLevel::Ok ? String("ok") : (h.risk == RiskLevel::Warning ? String("warning") : String("critical")),
        theme::riskColor(h.risk));

    gfx.setTextColor(theme::GREY, theme::BG);
    gfx.setCursor(6, 86);
    gfx.print("PORTS: not scanned yet");
    gfx.setCursor(6, 96);
    gfx.print("CRED AUDIT: not run");

    gfx.setCursor(4, gfx.height() - 9);
    gfx.print("ENTER/DEL:back");
}
