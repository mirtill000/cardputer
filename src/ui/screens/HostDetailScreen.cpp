#include "HostDetailScreen.h"
#include "PortScanScreen.h"
#include "CredAuditScreen.h"
#include "CredDisclaimerScreen.h"
#include "HttpBruteScreen.h"
#include "SmbScreen.h"
#include "MitmScreen.h"
#include "OffensiveDisclaimerScreen.h"
#include "../UiManager.h"
#include "../Theme.h"
#include "../Chrome.h"
#include "../../core/Config.h"
#include "../../core/Types.h"
#include "../../scan/ScanManager.h"
#include <cmath>

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
        return;
    }
    if (key == UiKey::Char && (ch == 'h' || ch == 'H')) {
        HostInfo h;
        if (!g_scanManager.getHost(_hostIndex, h)) return;
        for (const auto& p : h.ports) {
            if (p.service == "http") {
                HttpBruteScreen::instance().setTarget(h.ip, p.port);
                g_ui.pushScreen(&HttpBruteScreen::instance());
                break;
            }
        }
        return;
    }
    if (key == UiKey::Char && (ch == 's' || ch == 'S')) {
        HostInfo h;
        if (!g_scanManager.getHost(_hostIndex, h)) return;
        for (const auto& p : h.ports) {
            if (p.service == "smb" || p.service == "netbios-ssn") {
                SmbScreen::instance().setTarget(h.ip, p.port);
                g_ui.pushScreen(&SmbScreen::instance());
                break;
            }
        }
        return;
    }
    if (key == UiKey::Char && (ch == 'm' || ch == 'M')) {
        HostInfo h;
        if (!g_scanManager.getHost(_hostIndex, h)) return;

        MitmScreen::instance().setTarget(h.ip);
        if (g_config.offensiveEnabled) {
            g_ui.pushScreen(&MitmScreen::instance());
        } else {
            OffensiveDisclaimerScreen::instance().setPendingTargetScreen(&MitmScreen::instance());
            g_ui.pushScreen(&OffensiveDisclaimerScreen::instance());
        }
    }
}

namespace {
constexpr float kTwoPi = 6.28318530718f;  // avoids relying on M_PI's availability

void row(M5Canvas& gfx, int16_t y, const char* label, const String& value, uint16_t valueColor) {
    gfx.setTextColor(theme::GREY, theme::BG);
    gfx.setCursor(6, y);
    gfx.print(label);
    gfx.setTextColor(valueColor, theme::BG);
    gfx.setCursor(6 + 11 * theme::GLYPH_W, y);
    gfx.print(value);
}

// Purely decorative radar panel — devices don't have real spatial
// coordinates, so neither the "blips" nor their bearing mean anything;
// it's the same idea as the mockup's radar. Drawn last so its
// background fill also cleans up any long field (e.g. VENDOR:) that
// spilled into this area from the text rows above.
void drawRadar(M5Canvas& gfx, const HostInfo& h) {
    constexpr int16_t kPanelX = 150, kPanelY = 17, kPanelW = 90, kPanelH = 82;
    constexpr int16_t cx = kPanelX + kPanelW / 2;
    constexpr int16_t cy = kPanelY + kPanelH / 2;
    constexpr int16_t kRMax = 30;

    gfx.fillRect(kPanelX, kPanelY, kPanelW, kPanelH, theme::BG);
    gfx.drawRect(kPanelX, kPanelY, kPanelW, kPanelH, theme::GREY);

    for (int16_t r = 10; r <= kRMax; r += 10) {
        gfx.drawCircle(cx, cy, r, theme::GREEN_DIM);
    }
    gfx.drawFastHLine(cx - kRMax, cy, kRMax * 2, theme::GREEN_DIM);
    gfx.drawFastVLine(cx, cy - kRMax, kRMax * 2, theme::GREEN_DIM);

    // Sweep line: one full rotation every ~3s.
    float angle = (float)(millis() % 3000) / 3000.0f * kTwoPi;
    int16_t sx = cx + (int16_t)(kRMax * cosf(angle));
    int16_t sy = cy + (int16_t)(kRMax * sinf(angle));
    gfx.drawLine(cx, cy, sx, sy, theme::CYAN);

    // A handful of "blips" at positions derived from this host's own IP
    // bytes, so they stay put across frames (no jitter) and differ from
    // host to host without claiming any real meaning.
    uint32_t seed = (uint32_t)h.ip[0] * 7919u + (uint32_t)h.ip[1] * 104729u +
                     (uint32_t)h.ip[2] * 15485863u + (uint32_t)h.ip[3];
    for (int i = 0; i < 4; i++) {
        seed = seed * 1103515245u + 12345u;
        float a = (float)(seed % 360) * kTwoPi / 360.0f;
        seed = seed * 1103515245u + 12345u;
        int16_t r = 8 + (int16_t)(seed % (kRMax - 8));
        int16_t bx = cx + (int16_t)(r * cosf(a));
        int16_t by = cy + (int16_t)(r * sinf(a));
        gfx.fillCircle(bx, by, 2, theme::MAGENTA);
    }

    gfx.fillCircle(cx, cy, 2, theme::GREEN_BRIGHT);  // "you are here"
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

    if (h.vulnNote.length()) {
        gfx.setTextColor(theme::RED, theme::BG);
        gfx.setCursor(6, 106);
        gfx.print("VULN: ");
        String note = h.vulnNote;
        if (note.length() > 34) note = note.substring(0, 34) + "...";
        gfx.print(note);
    }

    drawRadar(gfx, h);

    bool hasHttp = false, hasSmb = false;
    for (const auto& p : h.ports) {
        if (p.service == "http") hasHttp = true;
        if (p.service == "smb" || p.service == "netbios-ssn") hasSmb = true;
    }
    String actionHint;
    if (hasHttp) actionHint += "H:http-brute  ";
    actionHint += "M:mitm-audit";
    if (hasSmb) actionHint += "  S:smb-neg";
    gfx.setTextColor(theme::AMBER, theme::BG);
    gfx.setCursor(6, 116);
    gfx.print(actionHint);

    gfx.setTextColor(theme::GREY, theme::BG);
    gfx.setCursor(4, gfx.height() - 9);
    gfx.print("TAB:ports  C:creds  DEL:back");
}
