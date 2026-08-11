#include "HostDetailScreen.h"
#include "PortScanScreen.h"
#include "CredAuditScreen.h"
#include "CredDisclaimerScreen.h"
#include "../UiManager.h"
#include "../Theme.h"
#include "../Chrome.h"
#include "../../core/Config.h"
#include "../../core/Types.h"
#include "../../scan/ScanManager.h"

HostDetailScreen& HostDetailScreen::instance() {
    static HostDetailScreen s;
    return s;
}

void HostDetailScreen::onKey(UiKey key, char ch) {
    if (key == UiKey::Back) {
        g_ui.popScreen();
        return;
    }
    if (key == UiKey::Tab) {
        HostInfo h;
        if (g_scanManager.getHost(_hostIndex, h)) {
            PortScanScreen::instance().setTarget(h.ip);
            g_ui.pushScreen(&PortScanScreen::instance());
        }
        return;
    }
    if (key == UiKey::Char && (ch == 'c' || ch == 'C')) {
        HostInfo h;
        if (!g_scanManager.getHost(_hostIndex, h)) return;

        if (g_config.credAuditEnabled) {
            CredAuditScreen::instance().setTarget(h.ip);
            g_ui.pushScreen(&CredAuditScreen::instance());
        } else {
            CredDisclaimerScreen::instance().setPendingTarget(h.ip);
            g_ui.pushScreen(&CredDisclaimerScreen::instance());
        }
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

    chrome::drawHeader(gfx, "HOST DETAIL");

    row(gfx, 20, "IP:", h.ip.toString(), theme::GREEN);
    row(gfx, 30, "MAC:", h.macKnown ? macToString(h.mac) : String("unknown"), theme::GREEN);
    row(gfx, 40, "HOST:", h.hostname.length() ? h.hostname : String("-"), theme::GREEN);
    row(gfx, 50, "VENDOR:", h.vendor.length() ? h.vendor : String("unknown"), theme::GREEN);
    row(gfx, 60, "CLASS:", String(deviceClassLabel(h.deviceClass)), theme::CYAN);
    row(gfx, 70, "RISK:", h.risk == RiskLevel::Ok ? String("ok") : (h.risk == RiskLevel::Warning ? String("warning") : String("critical")),
        theme::riskColor(h.risk));

    gfx.setTextColor(theme::GREY, theme::BG);
    gfx.setCursor(6, 86);
    if (h.ports.empty()) {
        gfx.print("PORTS: not scanned (TAB)");
    } else {
        gfx.print("PORTS: ");
        gfx.print((unsigned)h.ports.size());
        gfx.print(" open (TAB to rescan)");
    }

    gfx.setCursor(6, 96);
    if (!h.credAudited) {
        gfx.print("CRED AUDIT: not run (C)");
    } else if (h.credVulnerable) {
        gfx.setTextColor(theme::RED, theme::BG);
        gfx.print("CRED AUDIT: VULNERABLE (C)");
    } else {
        gfx.setTextColor(theme::GREEN, theme::BG);
        gfx.print("CRED AUDIT: clean (C)");
    }

    gfx.setTextColor(theme::GREY, theme::BG);
    gfx.setCursor(4, gfx.height() - 9);
    gfx.print("TAB:ports  C:creds  DEL:back");
}
