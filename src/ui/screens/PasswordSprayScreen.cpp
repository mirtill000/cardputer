#include "PasswordSprayScreen.h"
#include "../UiManager.h"
#include "../Theme.h"
#include "../Chrome.h"
#include "../TextWrap.h"
#include "../../core/Config.h"
#include "../../scan/PasswordSprayManager.h"

namespace {
const char* kDisclaimer =
    "Attempts ONE password across MANY discovered hosts / users, spread "
    "out to stay under account-lockout thresholds. Real login attempts. "
    "Use ONLY on networks/accounts you own or are explicitly authorized "
    "to test - unauthorized use is illegal and (unlike brute force) can "
    "look benign to defenders, which is exactly why it is easy to abuse.";
}  // namespace

PasswordSprayScreen& PasswordSprayScreen::instance() {
    static PasswordSprayScreen s;
    return s;
}

void PasswordSprayScreen::onEnter() {
    _consented = g_config.credAuditEnabled;
    _typing = true;
    _selected = 0;
    if (_consented) g_ui.setTextEntryMode(true);
}

void PasswordSprayScreen::onExit() {
    g_ui.setTextEntryMode(false);
}

void PasswordSprayScreen::onScanEvent(const ScanNotification& ev) {
    (void)ev;  // pulled live from the manager each draw
}

void PasswordSprayScreen::onKey(UiKey key, char ch) {
    if (!_consented) {
        if (key == UiKey::Char && (ch == 'y' || ch == 'Y')) {
            g_config.credAuditEnabled = true;
            g_config.credAuditAcknowledged = true;
            g_config.save();
            _consented = true;
            _typing = true;
            g_ui.setTextEntryMode(true);
        } else if (key == UiKey::Back) {
            g_ui.popScreen();
        }
        return;
    }

    if (_typing) {
        // Typing the password (text-entry mode: arrows arrive as Char).
        if (key == UiKey::Char) {
            if (_password.length() < 40) _password += ch;
        } else if (key == UiKey::Enter) {
            if (!_password.isEmpty()) {
                g_ui.setTextEntryMode(false);
                _typing = false;
                if (!g_passwordSpray.isRunning()) g_passwordSpray.start(_password);
            }
        } else if (key == UiKey::Back) {
            if (_password.length() > 0) {
                _password.remove(_password.length() - 1);
            } else {
                g_ui.setTextEntryMode(false);
                g_ui.popScreen();
            }
        }
        return;
    }

    // Browsing the results.
    if (key == UiKey::Up) {
        if (_selected > 0) _selected--;
    } else if (key == UiKey::Down) {
        if (_selected + 1 < g_passwordSpray.count()) _selected++;
    } else if (key == UiKey::Enter) {
        // Allow re-run with the SAME password (a re-run with a DIFFERENT
        // password is what "back to edit" is for - see DEL below).
        if (!g_passwordSpray.isRunning()) g_passwordSpray.start(_password);
    } else if (key == UiKey::Back) {
        // Edit the password again. Cancels any in-flight run so it doesn't
        // keep spraying under the old password while the user types a new
        // one — a spray change mid-flight would be extremely surprising.
        if (g_passwordSpray.isRunning()) g_passwordSpray.stop();
        _typing = true;
        g_ui.setTextEntryMode(true);
    }
}

void PasswordSprayScreen::draw(M5Canvas& gfx) {
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

    chrome::drawHeader(gfx, "PASSWORD SPRAY");

    // Password field (always visible so the user knows what they're
    // spraying, even after they hit ENTER and switched to browse mode).
    gfx.setTextColor(theme::GREY, theme::BG);
    gfx.setCursor(6, 18);
    gfx.print("pw: ");
    gfx.fillRect(28, 17, gfx.width() - 34, 10, theme::PANEL_BG);
    gfx.setTextColor(theme::GREEN_BRIGHT, theme::PANEL_BG);
    gfx.setCursor(30, 18);
    gfx.print(_password);
    if (_typing && (millis() / 500) % 2 == 0) {
        gfx.setCursor(30 + (int16_t)_password.length() * theme::GLYPH_W, 18);
        gfx.print("_");
    }

    bool running = g_passwordSpray.isRunning();
    gfx.setTextColor(running ? theme::CYAN : theme::GREEN, theme::BG);
    gfx.setCursor(6, 30);
    gfx.print(_typing ? "[typing]" : (running ? "[spraying]" : "[done]"));
    gfx.setTextColor(theme::GREY, theme::BG);
    gfx.print("  att:");
    gfx.print((unsigned)g_passwordSpray.attempts());
    gfx.print(" tgt:");
    gfx.print((unsigned)g_passwordSpray.targets());
    gfx.print(" hits:");
    gfx.setTextColor((g_passwordSpray.count() > 0) ? theme::RED : theme::GREEN, theme::BG);
    gfx.print((unsigned)g_passwordSpray.count());

    drawList(gfx, 42);

    gfx.setTextColor(theme::GREY, theme::BG);
    gfx.setCursor(4, gfx.height() - 9);
    gfx.print(_typing ? "type + ENTER  DEL:back/clear" : "ENTER:re-run DEL:edit ?:help");
}

void PasswordSprayScreen::drawList(M5Canvas& gfx, int16_t top) {
    size_t count = g_passwordSpray.count();
    if (count == 0) {
        if (!g_passwordSpray.isRunning() && !_typing)
            chrome::drawEmptyState(gfx, "no hits", "run NETWORK SCAN + port scan first");
        return;
    }

    gfx.drawFastHLine(4, top, gfx.width() - 8, theme::GREY);

    constexpr int16_t kRowH = 11;
    constexpr size_t kMaxRows = 6;
    size_t first = 0;
    if (_selected >= kMaxRows) first = _selected - kMaxRows + 1;

    PasswordSprayManager::Hit h;
    for (size_t row = 0; row < kMaxRows; row++) {
        size_t i = first + row;
        if (i >= count) break;
        if (!g_passwordSpray.get(i, h)) continue;

        int16_t y = top + 2 + (int16_t)row * kRowH;
        bool sel = (!_typing && i == _selected);
        uint16_t rowBg = sel ? theme::PANEL_BG : theme::BG;
        if (sel) gfx.fillRect(0, y, gfx.width(), kRowH, rowBg);

        gfx.setTextColor(sel ? theme::CYAN : theme::RED, rowBg);
        gfx.setCursor(6, y);
        gfx.print(h.ip.toString());

        gfx.setTextColor(sel ? theme::CYAN : theme::AMBER, rowBg);
        gfx.setCursor(108, y);
        String line = h.service + " " + h.user;
        if (line.length() > 22) line = line.substring(0, 22);
        gfx.print(line);
    }
}
