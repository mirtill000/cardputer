#include "IotCredScreen.h"
#include "../UiManager.h"
#include "../Theme.h"
#include "../Chrome.h"
#include "../TextWrap.h"
#include "../../core/Config.h"
#include "../../scan/IotCredScanner.h"

namespace {
const char* kDisclaimer =
    "Fingerprints discovered hosts and actively tries their documented "
    "factory-default logins (HTTP/Telnet), rate-limited, stopping on the "
    "first success per service. Real login attempts against real devices - "
    "use ONLY on hosts you own or are explicitly authorized to test.";
}  // namespace

IotCredScreen& IotCredScreen::instance() {
    static IotCredScreen s;
    return s;
}

void IotCredScreen::onEnter() {
    _consented = g_config.credAuditEnabled;
    _selected = 0;
}

void IotCredScreen::onScanEvent(const ScanNotification& ev) {
    (void)ev;  // list pulled live from the scanner each draw
}

void IotCredScreen::onKey(UiKey key, char ch) {
    if (!_consented) {
        if (key == UiKey::Char && (ch == 'y' || ch == 'Y')) {
            g_config.credAuditEnabled = true;
            g_config.credAuditAcknowledged = true;
            g_config.save();
            _consented = true;
        } else if (key == UiKey::Back) {
            g_ui.popScreen();
        }
        return;
    }

    if (key == UiKey::Enter) {
        if (!g_iotCredScanner.isRunning()) g_iotCredScanner.start();
    } else if (key == UiKey::Up) {
        if (_selected > 0) _selected--;
    } else if (key == UiKey::Down) {
        if (_selected + 1 < g_iotCredScanner.count()) _selected++;
    } else if (key == UiKey::Back) {
        g_ui.popScreen();
    }
}

void IotCredScreen::draw(M5Canvas& gfx) {
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

    chrome::drawHeader(gfx, "IOT CREDS");

    bool running = g_iotCredScanner.isRunning();
    gfx.setTextColor(running ? theme::CYAN : theme::GREEN, theme::BG);
    gfx.setCursor(6, 18);
    gfx.print("attempts: ");
    gfx.print((unsigned)g_iotCredScanner.attemptCount());
    gfx.print("  hits: ");
    gfx.setTextColor((g_iotCredScanner.count() > 0) ? theme::RED : theme::GREEN, theme::BG);
    gfx.print((unsigned)g_iotCredScanner.count());
    if (running) {
        gfx.setTextColor(theme::CYAN, theme::BG);
        gfx.print("  [sweeping...]");
    }

    drawList(gfx, 30);

    gfx.setTextColor(theme::GREY, theme::BG);
    gfx.setCursor(4, gfx.height() - 9);
    gfx.print(running ? "trying defaults..." : "ENTER:sweep ?:help DEL:back");
}

void IotCredScreen::drawList(M5Canvas& gfx, int16_t top) {
    size_t count = g_iotCredScanner.count();
    if (count == 0) {
        if (!g_iotCredScanner.isRunning())
            chrome::drawEmptyState(gfx, "no hits yet", "run NETWORK SCAN then ENTER");
        return;
    }

    gfx.drawFastHLine(4, top, gfx.width() - 8, theme::GREY);

    constexpr int16_t kRowH = 11;
    constexpr size_t kMaxRows = 8;
    size_t first = 0;
    if (_selected >= kMaxRows) first = _selected - kMaxRows + 1;

    IotCredScanner::Hit h;
    for (size_t row = 0; row < kMaxRows; row++) {
        size_t i = first + row;
        if (i >= count) break;
        if (!g_iotCredScanner.get(i, h)) continue;

        int16_t y = top + 2 + (int16_t)row * kRowH;
        bool sel = (i == _selected);
        uint16_t rowBg = sel ? theme::PANEL_BG : theme::BG;
        if (sel) gfx.fillRect(0, y, gfx.width(), kRowH, rowBg);

        gfx.setTextColor(sel ? theme::CYAN : theme::RED, rowBg);
        gfx.setCursor(6, y);
        gfx.print(h.ip.toString());

        gfx.setTextColor(sel ? theme::CYAN : theme::AMBER, rowBg);
        gfx.setCursor(108, y);
        String combo = h.service + " " + h.user + "/" + (h.pass.length() ? h.pass : String("<blank>"));
        if (combo.length() > 22) combo = combo.substring(0, 22);
        gfx.print(combo);
    }
}
