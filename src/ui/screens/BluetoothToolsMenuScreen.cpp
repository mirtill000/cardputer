#include "BluetoothToolsMenuScreen.h"
#include "BleScannerScreen.h"
#include "BleHidScreen.h"
#include "BleTrackerScreen.h"
#include "../UiManager.h"
#include "../Theme.h"
#include "../Chrome.h"

namespace {
struct Item {
    const char* label;
    Screen* (*get)();
};
Screen* gBleScan() { return &BleScannerScreen::instance(); }
Screen* gBleHid() { return &BleHidScreen::instance(); }
Screen* gBleTrackers() { return &BleTrackerScreen::instance(); }
const Item kItems[] = {
    {"BLE SCAN", gBleScan},
    {"BLE HID", gBleHid},
    {"BLE TRACKERS", gBleTrackers},
};
constexpr size_t kCount = sizeof(kItems) / sizeof(kItems[0]);
}  // namespace

BluetoothToolsMenuScreen& BluetoothToolsMenuScreen::instance() {
    static BluetoothToolsMenuScreen s;
    return s;
}

void BluetoothToolsMenuScreen::onEnter() {
    if (_selected >= kCount) _selected = 0;
}

void BluetoothToolsMenuScreen::onKey(UiKey key, char /*ch*/) {
    switch (key) {
        case UiKey::Up:
            _selected = (_selected == 0) ? kCount - 1 : _selected - 1;
            break;
        case UiKey::Down:
            _selected = (_selected + 1) % kCount;
            break;
        case UiKey::Enter:
            g_ui.pushScreen(kItems[_selected].get());
            break;
        case UiKey::Back:
            g_ui.popScreen();
            break;
        default:
            break;
    }
}

void BluetoothToolsMenuScreen::draw(M5Canvas& gfx) {
    gfx.fillScreen(theme::BG);
    chrome::drawHeader(gfx, "BLUETOOTH TOOLS");

    constexpr int16_t kRowH = 20;
    constexpr int16_t kTop = 20;
    for (size_t i = 0; i < kCount; i++) {
        int16_t y = kTop + (int16_t)i * kRowH;
        bool sel = (i == _selected);
        uint16_t rowBg = sel ? theme::PANEL_BG : theme::BG;
        gfx.drawRect(4, y, gfx.width() - 8, kRowH - 3, sel ? theme::CYAN : theme::MAGENTA);
        gfx.fillRect(5, y + 1, gfx.width() - 10, kRowH - 5, rowBg);

        gfx.setTextColor(sel ? theme::CYAN : theme::MAGENTA, rowBg);
        gfx.setCursor(12, y + 4);
        gfx.print(sel ? "> " : "  ");
        gfx.setTextColor(sel ? theme::CYAN : theme::MAGENTA, rowBg);
        gfx.print(kItems[i].label);
        // Right-side chevron.
        gfx.setCursor(gfx.width() - 12, y + 4);
        gfx.print(">");
    }

    gfx.setTextColor(theme::GREY, theme::BG);
    gfx.setCursor(4, gfx.height() - 9);
    gfx.print("ENTER:open  DEL:back");
}
