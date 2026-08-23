#include "BleDetailScreen.h"
#include "BleGattScreen.h"
#include "../UiManager.h"
#include "../Theme.h"
#include "../Chrome.h"
#include "../../scan/BluetoothManager.h"

BleDetailScreen& BleDetailScreen::instance() {
    static BleDetailScreen s;
    return s;
}

void BleDetailScreen::onKey(UiKey key, char ch) {
    if (key == UiKey::Char && (ch == 'g' || ch == 'G')) {
        // Fase 53 - open the GATT walk against this device (gated
        // inside BleGattScreen itself).
        BleGattScreen::instance().setTarget(_addr);
        g_ui.pushScreen(&BleGattScreen::instance());
        return;
    }
    if (key == UiKey::Back) g_ui.popScreen();
}

namespace {
const char* addrKindLabel(BluetoothManager::AddrKind k) {
    switch (k) {
        case BluetoothManager::AddrKind::Public: return "public";
        case BluetoothManager::AddrKind::Random: return "random (non-res.)";
        case BluetoothManager::AddrKind::RandomStatic: return "random-static";
        case BluetoothManager::AddrKind::Rpa: return "RPA (rotates)";
        default: return "?";
    }
}

// Two-column label/value row. Keeps the detail view visually consistent
// with the credentials / port results panels elsewhere in the app.
void row(M5Canvas& gfx, int16_t y, const char* label, const String& value, uint16_t valueColor) {
    gfx.setTextColor(theme::GREY, theme::BG);
    gfx.setCursor(6, y);
    gfx.print(label);
    gfx.setTextColor(valueColor, theme::BG);
    gfx.setCursor(6 + 9 * theme::GLYPH_W, y);
    String v = value;
    if (v.length() > 30) v = v.substring(0, 30);
    gfx.print(v);
}
}  // namespace

void BleDetailScreen::draw(M5Canvas& gfx) {
    gfx.fillScreen(theme::BG);
    chrome::drawHeader(gfx, "BLE DETAIL");

    // Locate the device by address, walking the manager's device list
    // rather than caching a pointer - the list is mutex-protected and
    // could grow between our getting an index and reading it.
    BluetoothManager::BleDevice d;
    bool found = false;
    for (size_t i = 0, n = g_bluetoothManager.deviceCount(); i < n; i++) {
        if (!g_bluetoothManager.get(i, d)) continue;
        if (d.addr == _addr) { found = true; break; }
    }
    if (!found) {
        chrome::drawEmptyState(gfx, "device dropped", "advertiser is no longer seen");
        gfx.setTextColor(theme::GREY, theme::BG);
        gfx.setCursor(4, gfx.height() - 9);
        gfx.print("DEL:back");
        return;
    }

    int16_t y = 18;
    row(gfx, y, "addr:", d.addr, theme::GREEN); y += 9;
    row(gfx, y, "type:", addrKindLabel(d.addrKind), theme::CYAN); y += 9;
    char rb[24];
    snprintf(rb, sizeof(rb), "%d dBm  tx:%d", (int)d.rssi, (int)d.txPower);
    row(gfx, y, "rssi:", rb, theme::AMBER); y += 9;
    if (d.name.length()) { row(gfx, y, "name:", d.name, theme::GREEN); y += 9; }
    if (d.vendor.length()) { row(gfx, y, "vendor:", d.vendor, theme::CYAN); y += 9; }
    if (d.appearance) {
        char ab[16];
        snprintf(ab, sizeof(ab), "0x%04X", (unsigned)d.appearance);
        row(gfx, y, "appear:", ab, theme::GREY);
        y += 9;
    }
    if (d.services.length()) { row(gfx, y, "svc:", d.services, theme::GREY); y += 9; }
    if (d.platformNote.length()) { row(gfx, y, "note:", d.platformNote, theme::MAGENTA); y += 9; }
    if (d.beacon != BluetoothManager::BeaconKind::None) {
        row(gfx, y, "beacon:", d.beaconNote, theme::CYAN);
        y += 9;
    }
    if (d.tracker != BluetoothManager::TrackerKind::None) {
        row(gfx, y, "TRACK:", d.trackerNote, theme::RED);
        y += 9;
    }
    if (d.correlatedWifiIp.length()) {
        row(gfx, y, "wifi:", d.correlatedWifiIp, theme::MAGENTA);
        y += 9;
    }
    // Fase 53 - RPA rotation correlation.
    if (d.sameAsAddr.length()) {
        row(gfx, y, "same-as:", d.sameAsAddr, theme::MAGENTA);
        y += 9;
    }
    if (d.hidService) {
        row(gfx, y, "HID:", String("yes - input device"), theme::AMBER);
        y += 9;
    }
    // Sightings + first/last seen (compact).
    char sb[32];
    uint32_t ageSec = (millis() - d.lastSeenMs) / 1000;
    snprintf(sb, sizeof(sb), "%u ads, last %us ago", (unsigned)d.sightings, (unsigned)ageSec);
    row(gfx, y, "seen:", sb, theme::GREY);

    gfx.setTextColor(theme::GREY, theme::BG);
    gfx.setCursor(4, gfx.height() - 9);
    gfx.print("G:GATT walk  DEL:back  ?:help");
}
