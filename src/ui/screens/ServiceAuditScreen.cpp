#include "ServiceAuditScreen.h"
#include "../UiManager.h"
#include "../Theme.h"
#include "../Chrome.h"
#include "../TextWrap.h"
#include "../../core/Config.h"
#include "../../scan/ServiceAuditManager.h"

namespace {
const char* kDisclaimer =
    "Actively tries anonymous access and default/built-in credentials "
    "against this host's open services (FTP/SMB/Redis/MySQL/PostgreSQL/"
    "VNC/HTTP). This is a real attack tool. Use ONLY on hosts you own or "
    "are explicitly authorized to test - unauthorized use is illegal.";
}  // namespace

ServiceAuditScreen& ServiceAuditScreen::instance() {
    static ServiceAuditScreen s;
    return s;
}

void ServiceAuditScreen::beginAudit() {
    _selected = 0;
    if (!g_serviceAuditManager.isRunning()) g_serviceAuditManager.start(_target);
}

void ServiceAuditScreen::onEnter() {
    _consented = g_config.credAuditEnabled;
    if (_consented) beginAudit();
}

void ServiceAuditScreen::onScanEvent(const ScanNotification& ev) {
    (void)ev;  // findings pulled live from the manager each draw
}

void ServiceAuditScreen::onKey(UiKey key, char ch) {
    if (!_consented) {
        if (key == UiKey::Char && (ch == 'y' || ch == 'Y')) {
            g_config.credAuditEnabled = true;
            g_config.credAuditAcknowledged = true;
            g_config.save();
            _consented = true;
            beginAudit();
        } else if (key == UiKey::Back) {
            g_ui.popScreen();
        }
        return;
    }

    if (key == UiKey::Enter) {
        if (!g_serviceAuditManager.isRunning()) g_serviceAuditManager.start(_target);
    } else if (key == UiKey::Up) {
        if (_selected > 0) _selected--;
    } else if (key == UiKey::Down) {
        if (_selected + 1 < g_serviceAuditManager.count()) _selected++;
    } else if (key == UiKey::Back) {
        g_ui.popScreen();  // audit keeps running in the background if not done
    }
}

void ServiceAuditScreen::draw(M5Canvas& gfx) {
    gfx.fillScreen(theme::BG);

    if (!_consented) {
        chrome::drawAlertHeader(gfx, "AUTHORIZATION REQUIRED");
        gfx.setTextColor(theme::AMBER, theme::BG);
        drawWrapped(gfx, kDisclaimer, 6, 20, 10, 37);
        gfx.setTextColor(theme::MAGENTA, theme::BG);
        gfx.setCursor(6, 110);
        gfx.print("Y: I own/am authorized");
        gfx.setTextColor(theme::GREY, theme::BG);
        gfx.setCursor(6, 122);
        gfx.print("DEL: cancel");
        return;
    }

    chrome::drawHeader(gfx, "SERVICE AUDIT");

    bool running = g_serviceAuditManager.isRunning();
    gfx.setTextColor(theme::GREEN, theme::BG);
    gfx.setCursor(6, 18);
    gfx.print("target: ");
    gfx.print(_target.toString());

    gfx.setTextColor(running ? theme::CYAN : theme::GREEN, theme::BG);
    gfx.setCursor(6, 28);
    gfx.print("findings: ");
    gfx.print((unsigned)g_serviceAuditManager.count());
    gfx.print(running ? "  [auditing...]" : "");

    drawFindings(gfx, 38);

    gfx.setTextColor(theme::MAGENTA, theme::BG);
    gfx.setCursor(6, gfx.height() - 20);
    gfx.print(running ? "running..." : "ENTER: re-run");

    gfx.setTextColor(theme::GREY, theme::BG);
    gfx.setCursor(4, gfx.height() - 9);
    gfx.print("DEL:back (keeps running)");
}

void ServiceAuditScreen::drawFindings(M5Canvas& gfx, int16_t top) {
    size_t count = g_serviceAuditManager.count();
    if (count == 0) return;

    gfx.setTextColor(theme::GREY, theme::BG);
    gfx.drawFastHLine(4, top, gfx.width() - 8, theme::GREY);

    constexpr int16_t kRowH = 10;
    constexpr size_t kMaxRows = 6;

    size_t first = 0;
    if (_selected >= kMaxRows) first = _selected - kMaxRows + 1;

    ServiceAuditManager::Finding f;
    for (size_t row = 0; row < kMaxRows; row++) {
        size_t i = first + row;
        if (i >= count) break;
        if (!g_serviceAuditManager.get(i, f)) continue;

        int16_t y = top + 2 + (int16_t)row * kRowH;
        bool sel = (i == _selected);
        uint16_t rowBg = sel ? theme::PANEL_BG : theme::BG;
        if (sel) gfx.fillRect(0, y, gfx.width(), kRowH, rowBg);

        gfx.setTextColor(sel ? theme::CYAN : (f.critical ? theme::RED : theme::GREEN), rowBg);
        gfx.setCursor(6, y);
        String line = f.service + ": " + f.result;
        if (line.length() > 38) line = line.substring(0, 38);
        gfx.print(line);
    }

    chrome::drawScrollMarkers(gfx, top + 2, top + 2 + (int16_t)kMaxRows * kRowH, first > 0, (first + kMaxRows) < count);
}
