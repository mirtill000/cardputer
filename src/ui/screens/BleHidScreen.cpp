#include "BleHidScreen.h"
#include "BleGattScreen.h"
#include "../UiManager.h"
#include "../Theme.h"
#include "../Chrome.h"
#include "../../scan/BluetoothManager.h"

BleHidScreen& BleHidScreen::instance() {
    static BleHidScreen s;
    return s;
}

void BleHidScreen::onEnter() { _selected = 0; }

void BleHidScreen::onKey(UiKey key, char /*ch*/) {
    if (key == UiKey::Enter) {
        BluetoothManager::BleDevice d;
        if (g_bluetoothManager.getFirstHid(_selected, d)) {
            BleGattScreen::instance().setTarget(d.addr);
            g_ui.pushScreen(&BleGattScreen::instance());
        }
    } else if (key == UiKey::Up) {
        if (_selected > 0) _selected--;
    } else if (key == UiKey::Down) {
        if (_selected + 1 < g_bluetoothManager.hidCount()) _selected++;
    } else if (key == UiKey::Back) {
        g_ui.popScreen();
    }
}

void BleHidScreen::draw(M5Canvas& gfx) {
    gfx.fillScreen(theme::BG);
    chrome::drawHeader(gfx, "BLE HID");

    uint32_t total = g_bluetoothManager.hidCount();
    gfx.setTextColor((total > 0) ? theme::MAGENTA : theme::GREEN, theme::BG);
    gfx.setCursor(6, 18);
    gfx.print("HID advertisers: ");
    gfx.print((unsigned)total);

    if (total == 0) {
        chrome::drawEmptyState(gfx, "no HID", "waiting for keyboards/mice");
        gfx.setTextColor(theme::GREY, theme::BG);
        gfx.setCursor(4, gfx.height() - 9);
        gfx.print("DEL:back");
        return;
    }

    int16_t top = 30;
    gfx.drawFastHLine(4, top, gfx.width() - 8, theme::GREY);
    constexpr int16_t kRowH = 11;
    constexpr size_t kMaxRows = 7;
    size_t first = 0;
    if (_selected >= kMaxRows) first = _selected - kMaxRows + 1;

    BluetoothManager::BleDevice d;
    for (size_t row = 0; row < kMaxRows; row++) {
        size_t i = first + row;
        if (!g_bluetoothManager.getFirstHid(i, d)) break;
        int16_t y = top + 2 + (int16_t)row * kRowH;
        bool sel = (i == _selected);
        uint16_t rowBg = sel ? theme::PANEL_BG : theme::BG;
        if (sel) gfx.fillRect(0, y, gfx.width(), kRowH, rowBg);

        gfx.setTextColor(sel ? theme::CYAN : theme::GREEN, rowBg);
        gfx.setCursor(6, y);
        String tail = d.addr.substring(6);
        gfx.print(tail);

        gfx.setTextColor(sel ? theme::CYAN : theme::AMBER, rowBg);
        gfx.setCursor(80, y);
        char rb[8];
        snprintf(rb, sizeof(rb), "%d", (int)d.rssi);
        gfx.print(rb);

        gfx.setTextColor(sel ? theme::CYAN : theme::GREY, rowBg);
        gfx.setCursor(110, y);
        String label = d.name.length() ? d.name : (d.vendor.length() ? d.vendor : String("HID device"));
        if (label.length() > 20) label = label.substring(0, 20);
        gfx.print(label);
    }

    gfx.setTextColor(theme::GREY, theme::BG);
    gfx.setCursor(4, gfx.height() - 9);
    gfx.print("ENTER:GATT(gated)  DEL:back");
}
