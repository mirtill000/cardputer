#include "BleScannerScreen.h"
#include "BleDetailScreen.h"
#include "BleTrackerScreen.h"
#include "../UiManager.h"
#include "../Theme.h"
#include "../Chrome.h"
#include "../../scan/BluetoothManager.h"

BleScannerScreen& BleScannerScreen::instance() {
    static BleScannerScreen s;
    return s;
}

void BleScannerScreen::onEnter() {
    _selected = 0;
    // Auto-start the scanner - reaching this screen with no BLE session
    // running is the most common case, and there's no meaningful screen
    // content until at least one advertisement lands. Same "enter =
    // active" pattern the WiFi scan / war-driving views already use.
    if (!g_bluetoothManager.isRunning()) g_bluetoothManager.start();
}

void BleScannerScreen::onExit() {
    // Leave the scanner running/stopped as the user last set it via 'S'.
    // Matches wardriving / promiscuous sniffer screens - their managers
    // don't auto-stop on exit either; the header RF indicator reflects
    // the state.
}

void BleScannerScreen::onScanEvent(const ScanNotification& ev) {
    (void)ev;  // list pulled live from the manager each draw
}

void BleScannerScreen::onKey(UiKey key, char ch) {
    if (key == UiKey::Enter) {
        BluetoothManager::BleDevice d;
        if (g_bluetoothManager.get(_selected, d)) {
            BleDetailScreen::instance().setAddress(d.addr);
            g_ui.pushScreen(&BleDetailScreen::instance());
        }
    } else if (key == UiKey::Up) {
        if (_selected > 0) _selected--;
    } else if (key == UiKey::Down) {
        if (_selected + 1 < g_bluetoothManager.deviceCount()) _selected++;
    } else if (key == UiKey::Char && (ch == 't' || ch == 'T')) {
        g_ui.pushScreen(&BleTrackerScreen::instance());
    } else if (key == UiKey::Char && (ch == 's' || ch == 'S')) {
        if (g_bluetoothManager.isRunning()) g_bluetoothManager.stop();
        else g_bluetoothManager.start();
    } else if (key == UiKey::Back) {
        g_ui.popScreen();
    }
}

namespace {
const char* addrKindTag(BluetoothManager::AddrKind k) {
    switch (k) {
        case BluetoothManager::AddrKind::Public: return "pub";
        case BluetoothManager::AddrKind::Random: return "rnd";
        case BluetoothManager::AddrKind::RandomStatic: return "sta";
        case BluetoothManager::AddrKind::Rpa: return "rpa";
        default: return "?";
    }
}

uint16_t rssiColor(int8_t rssi) {
    if (rssi >= -60) return theme::GREEN;
    if (rssi >= -80) return theme::AMBER;
    return theme::RED;
}
}  // namespace

void BleScannerScreen::draw(M5Canvas& gfx) {
    gfx.fillScreen(theme::BG);
    chrome::drawHeader(gfx, "BLE SCAN");

    bool running = g_bluetoothManager.isRunning();
    gfx.setTextColor(running ? theme::CYAN : theme::GREEN, theme::BG);
    gfx.setCursor(6, 18);
    gfx.print(running ? "[scanning]" : "[idle]");
    gfx.setTextColor(theme::GREY, theme::BG);
    gfx.print("  dev:");
    gfx.print((unsigned)g_bluetoothManager.deviceCount());
    gfx.print(" beacon:");
    gfx.setTextColor((g_bluetoothManager.beaconCount() > 0) ? theme::CYAN : theme::GREY, theme::BG);
    gfx.print((unsigned)g_bluetoothManager.beaconCount());
    gfx.setTextColor(theme::GREY, theme::BG);
    gfx.print(" trk:");
    gfx.setTextColor((g_bluetoothManager.trackerCount() > 0) ? theme::RED : theme::GREY, theme::BG);
    gfx.print((unsigned)g_bluetoothManager.trackerCount());
    gfx.setTextColor(theme::GREY, theme::BG);
    gfx.print(" W:");
    gfx.setTextColor((g_bluetoothManager.correlatedCount() > 0) ? theme::MAGENTA : theme::GREY, theme::BG);
    gfx.print((unsigned)g_bluetoothManager.correlatedCount());

    drawList(gfx, 30);

    gfx.setTextColor(theme::GREY, theme::BG);
    gfx.setCursor(4, gfx.height() - 9);
    gfx.print(running ? "ENTER:detail T:trk S:stop ?:help" : "ENTER:detail T:trk S:start ?:help");
}

void BleScannerScreen::drawList(M5Canvas& gfx, int16_t top) {
    size_t count = g_bluetoothManager.deviceCount();
    if (count == 0) {
        if (!g_bluetoothManager.isRunning())
            chrome::drawEmptyState(gfx, "BLE off", "S to start scanning");
        else
            chrome::drawEmptyState(gfx, "listening...", "waiting for advertisers");
        return;
    }

    gfx.drawFastHLine(4, top, gfx.width() - 8, theme::GREY);

    constexpr int16_t kRowH = 11;
    constexpr size_t kMaxRows = 7;
    size_t first = 0;
    if (_selected >= kMaxRows) first = _selected - kMaxRows + 1;

    BluetoothManager::BleDevice d;
    for (size_t row = 0; row < kMaxRows; row++) {
        size_t i = first + row;
        if (i >= count) break;
        if (!g_bluetoothManager.get(i, d)) continue;

        int16_t y = top + 2 + (int16_t)row * kRowH;
        bool sel = (i == _selected);
        uint16_t rowBg = sel ? theme::PANEL_BG : theme::BG;
        if (sel) gfx.fillRect(0, y, gfx.width(), kRowH, rowBg);

        // RSSI dBm (compact) - left column.
        gfx.setTextColor(sel ? theme::CYAN : rssiColor(d.rssi), rowBg);
        gfx.setCursor(6, y);
        char buf[8];
        snprintf(buf, sizeof(buf), "%4d", (int)d.rssi);
        gfx.print(buf);

        // Addr type (rpa/rnd/pub) then last 5 bytes of the address for
        // compactness - full 17-char MACs don't fit the row.
        gfx.setTextColor(sel ? theme::CYAN : theme::GREY, rowBg);
        gfx.setCursor(34, y);
        gfx.print(addrKindTag(d.addrKind));
        String tail = d.addr.substring(6);  // "cc:dd:ee:ff" (drops first 2 bytes)
        gfx.setTextColor(sel ? theme::CYAN : theme::GREEN, rowBg);
        gfx.setCursor(58, y);
        if (tail.length() > 14) tail = tail.substring(0, 14);
        gfx.print(tail);

        // Vendor/name/tag - right column, priority: name > tracker > beacon > vendor.
        String tag;
        uint16_t tagColor = sel ? theme::CYAN : theme::AMBER;
        if (d.tracker != BluetoothManager::TrackerKind::None) {
            tag = "!trk";
            tagColor = sel ? theme::CYAN : theme::RED;
        } else if (d.beacon != BluetoothManager::BeaconKind::None) {
            tag = "bcn";
            tagColor = sel ? theme::CYAN : theme::CYAN;
        } else if (d.name.length()) {
            tag = d.name;
        } else if (d.vendor.length()) {
            tag = d.vendor;
        } else {
            tag = "-";
            tagColor = sel ? theme::CYAN : theme::GREY;
        }
        if (tag.length() > 12) tag = tag.substring(0, 12);
        gfx.setTextColor(tagColor, rowBg);
        gfx.setCursor(150, y);
        gfx.print(tag);

        // WiFi correlation marker (feature #10).
        if (d.correlatedWifiIp.length()) {
            gfx.setTextColor(sel ? theme::CYAN : theme::MAGENTA, rowBg);
            gfx.setCursor(228, y);
            gfx.print("W");
        }
    }
}
