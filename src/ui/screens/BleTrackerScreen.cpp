#include "BleTrackerScreen.h"
#include "BleDetailScreen.h"
#include "../UiManager.h"
#include "../Theme.h"
#include "../Chrome.h"
#include "../../scan/BluetoothManager.h"

BleTrackerScreen& BleTrackerScreen::instance() {
    static BleTrackerScreen s;
    return s;
}

void BleTrackerScreen::onEnter() {
    _selected = 0;
}

void BleTrackerScreen::onKey(UiKey key, char /*ch*/) {
    if (key == UiKey::Enter) {
        BluetoothManager::BleDevice d;
        if (g_bluetoothManager.getFirstTracker(_selected, d)) {
            BleDetailScreen::instance().setAddress(d.addr);
            g_ui.pushScreen(&BleDetailScreen::instance());
        }
    } else if (key == UiKey::Up) {
        if (_selected > 0) _selected--;
    } else if (key == UiKey::Down) {
        // Bound isn't cheap to compute exactly without a second pass;
        // capping to trackerCount() is safe (getFirstTracker returns
        // false past the end and the list-draw loop stops early).
        if (_selected + 1 < g_bluetoothManager.trackerCount()) _selected++;
    } else if (key == UiKey::Back) {
        g_ui.popScreen();
    }
}

void BleTrackerScreen::draw(M5Canvas& gfx) {
    gfx.fillScreen(theme::BG);
    chrome::drawHeader(gfx, "BLE TRACKERS");

    uint32_t total = g_bluetoothManager.trackerCount();
    gfx.setTextColor((total > 0) ? theme::RED : theme::GREEN, theme::BG);
    gfx.setCursor(6, 18);
    gfx.print("trackers seen: ");
    gfx.print((unsigned)total);

    if (total == 0) {
        chrome::drawEmptyState(gfx, "no trackers", "AirTag/Tile/SmartTag ads will land here");
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
        if (!g_bluetoothManager.getFirstTracker(i, d)) break;
        int16_t y = top + 2 + (int16_t)row * kRowH;
        bool sel = (i == _selected);
        uint16_t rowBg = sel ? theme::PANEL_BG : theme::BG;
        if (sel) gfx.fillRect(0, y, gfx.width(), kRowH, rowBg);

        gfx.setTextColor(sel ? theme::CYAN : theme::RED, rowBg);
        gfx.setCursor(6, y);
        String addr = d.addr.substring(9);  // last 3 bytes
        gfx.print(addr);

        gfx.setTextColor(sel ? theme::CYAN : theme::AMBER, rowBg);
        gfx.setCursor(60, y);
        char rb[8];
        snprintf(rb, sizeof(rb), "%d", (int)d.rssi);
        gfx.print(rb);

        gfx.setTextColor(sel ? theme::CYAN : theme::GREY, rowBg);
        gfx.setCursor(90, y);
        String note = d.trackerNote;
        if (note.length() > 22) note = note.substring(0, 22);
        gfx.print(note);
    }

    gfx.setTextColor(theme::GREY, theme::BG);
    gfx.setCursor(4, gfx.height() - 9);
    gfx.print("ENTER:detail  DEL:back");
}
