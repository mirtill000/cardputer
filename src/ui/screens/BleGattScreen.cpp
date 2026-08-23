#include "BleGattScreen.h"
#include "../UiManager.h"
#include "../Theme.h"
#include "../Chrome.h"
#include "../TextWrap.h"
#include "../../core/Config.h"
#include "../../scan/BleGattClient.h"

namespace {
const char* kDisclaimer =
    "Actively connects to this BLE device and walks its GATT services + "
    "characteristics. Detection only - never writes, never pairs, never "
    "actuates. Still an ACTIVE probe: the peer sees the connect. Use "
    "ONLY on devices you own or are explicitly authorized to test.";
}  // namespace

BleGattScreen& BleGattScreen::instance() {
    static BleGattScreen s;
    return s;
}

void BleGattScreen::onEnter() {
    _consented = g_config.credAuditEnabled;
    _selected = 0;
    if (_consented && !g_bleGattClient.isRunning() && _target.length()) g_bleGattClient.start(_target);
}

void BleGattScreen::onScanEvent(const ScanNotification& ev) {
    (void)ev;  // pulled live from the client each draw
}

void BleGattScreen::onKey(UiKey key, char ch) {
    if (!_consented) {
        if (key == UiKey::Char && (ch == 'y' || ch == 'Y')) {
            g_config.credAuditEnabled = true;
            g_config.credAuditAcknowledged = true;
            g_config.save();
            _consented = true;
            if (!g_bleGattClient.isRunning() && _target.length()) g_bleGattClient.start(_target);
        } else if (key == UiKey::Back) {
            g_ui.popScreen();
        }
        return;
    }
    if (key == UiKey::Enter) {
        if (!g_bleGattClient.isRunning() && _target.length()) g_bleGattClient.start(_target);
    } else if (key == UiKey::Up) {
        if (_selected > 0) _selected--;
    } else if (key == UiKey::Down) {
        BleGattClient::WalkResult r = g_bleGattClient.result();
        if (_selected + 1 < r.services.size()) _selected++;
    } else if (key == UiKey::Back) {
        g_ui.popScreen();
    }
}

void BleGattScreen::draw(M5Canvas& gfx) {
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

    chrome::drawHeader(gfx, "BLE GATT");
    bool running = g_bleGattClient.isRunning();
    BleGattClient::WalkResult r = g_bleGattClient.result();

    gfx.setTextColor(theme::GREY, theme::BG);
    gfx.setCursor(6, 18);
    gfx.print("target: ");
    gfx.setTextColor(theme::GREEN, theme::BG);
    gfx.print(r.targetAddr);

    // Status row: connected / failed / pending, with DIS strings when we have them.
    gfx.setCursor(6, 28);
    if (running) {
        gfx.setTextColor(theme::CYAN, theme::BG);
        gfx.print("[walking...] svc:");
        gfx.print((unsigned)r.services.size());
    } else if (r.failureNote.length()) {
        gfx.setTextColor(theme::RED, theme::BG);
        String note = r.failureNote;
        if (note.length() > 36) note = note.substring(0, 36);
        gfx.print(note);
    } else if (r.connected) {
        gfx.setTextColor(theme::GREEN, theme::BG);
        gfx.print("done  svc:");
        gfx.print((unsigned)r.services.size());
        gfx.print(" wr:");
        gfx.print((unsigned)r.writableCharCount);
        gfx.print(" no-auth:");
        gfx.setTextColor((r.writableNoAuthCount > 0) ? theme::AMBER : theme::GREY, theme::BG);
        gfx.print((unsigned)r.writableNoAuthCount);
    } else {
        gfx.setTextColor(theme::GREY, theme::BG);
        gfx.print("idle - ENTER to walk");
    }

    // Device Info Service one-liner - one of the highest-signal outputs
    // of the walk (real firmware version, manufacturer name, etc.).
    if (r.diManufacturer.length() || r.diModel.length() || r.diFirmware.length()) {
        gfx.setTextColor(theme::CYAN, theme::BG);
        gfx.setCursor(6, 38);
        String di = r.diManufacturer + " " + r.diModel;
        if (r.diFirmware.length()) di += " fw:" + r.diFirmware;
        if (di.length() > 38) di = di.substring(0, 38);
        gfx.print(di);
    }

    // Feature #8 + #9 summary chips (compact).
    int16_t chipY = 48;
    if (r.knownControlWritable > 0) {
        gfx.setTextColor(theme::RED, theme::BG);
        gfx.setCursor(6, chipY);
        gfx.print("!CTRL:");
        gfx.print((unsigned)r.knownControlWritable);
        gfx.print("/");
        gfx.print((unsigned)r.knownControlCount);
    } else if (r.knownControlCount > 0) {
        gfx.setTextColor(theme::AMBER, theme::BG);
        gfx.setCursor(6, chipY);
        gfx.print("ctrl-known:");
        gfx.print((unsigned)r.knownControlCount);
    }
    if (r.sawHidService) {
        gfx.setTextColor(theme::MAGENTA, theme::BG);
        gfx.setCursor(120, chipY);
        gfx.print("HID");
    }

    // Services list (scrollable).
    int16_t top = 60;
    gfx.drawFastHLine(4, top, gfx.width() - 8, theme::GREY);
    constexpr int16_t kRowH = 10;
    constexpr size_t kMaxRows = 6;
    size_t first = 0;
    if (_selected >= kMaxRows) first = _selected - kMaxRows + 1;
    for (size_t row = 0; row < kMaxRows; row++) {
        size_t i = first + row;
        if (i >= r.services.size()) break;
        const auto& svc = r.services[i];
        int16_t y = top + 2 + (int16_t)row * kRowH;
        bool sel = (i == _selected);
        uint16_t rowBg = sel ? theme::PANEL_BG : theme::BG;
        if (sel) gfx.fillRect(0, y, gfx.width(), kRowH, rowBg);

        // Row = service UUID, char count, any control-hit inside.
        bool ctrl = false;
        uint16_t wr = 0;
        for (const auto& ch : svc.chars) {
            if (ch.controlHit) ctrl = true;
            if (ch.access & ((uint8_t)BleGattClient::CharAccess::Write |
                              (uint8_t)BleGattClient::CharAccess::WriteNoResp))
                wr++;
        }
        uint16_t color = sel ? theme::CYAN : (ctrl ? theme::RED : theme::GREEN);
        gfx.setTextColor(color, rowBg);
        gfx.setCursor(6, y);
        gfx.print(svc.uuid);

        gfx.setTextColor(sel ? theme::CYAN : theme::GREY, rowBg);
        gfx.setCursor(80, y);
        gfx.print("ch:");
        gfx.print((unsigned)svc.chars.size());
        gfx.print("  wr:");
        gfx.print((unsigned)wr);

        if (ctrl) {
            gfx.setTextColor(sel ? theme::CYAN : theme::RED, rowBg);
            gfx.setCursor(180, y);
            gfx.print("!CTRL");
        }
    }

    gfx.setTextColor(theme::GREY, theme::BG);
    gfx.setCursor(4, gfx.height() - 9);
    gfx.print(running ? "walking..." : "ENTER:re-run  DEL:back");
}
