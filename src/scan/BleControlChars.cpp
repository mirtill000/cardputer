#include "BleControlChars.h"
#include <cstddef>  // size_t for kCount - same reason as BleCompanyIds.cpp

namespace ble_control_chars {

// Small, curated table. Entries are documented in comments so the risk
// tier is obvious to a reviewer. NOT exhaustive on purpose - the intent
// is to catch consumer-visible smart-home actuators (locks, plugs,
// bulbs, bands) whose known control characteristics happen to also be
// writable without authentication, NOT to fingerprint every possible
// vendor-specific control channel.
const Entry kEntries[] = {
    // Nordic UART Service - widely used custom UART "control" over BLE
    // by many DIY / hobby smart devices. Not itself a smart lock but a
    // generic writable control vector when exposed unauthenticated.
    {0, "6e400001-b5a3-f393-e0a9-e50e24dcca9e", 0,
     "6e400002-b5a3-f393-e0a9-e50e24dcca9e", Kind::Vendor, "Nordic UART TX (unauth control)"},

    // Xiaomi Mi service (0xFE95) - Mi Band, Mi Home devices. Write endpoint
    // 0x0001 to 0x0006 depending on model. Detection-only: presence of a
    // writable char under FE95 is the hint.
    {0xFE95, nullptr, 0x0001, nullptr, Kind::SmartBand, "Xiaomi Mi control"},

    // TP-Link Kasa BLE control service (0xFE1C, some Kasa smart plugs
    // fall back to BLE control when not paired to Wi-Fi).
    {0xFE1C, nullptr, 0, "0000fff1-0000-1000-8000-00805f9b34fb", Kind::SmartPlug,
     "TP-Link Kasa BLE control"},

    // Generic FFF0/FFF1 pair - Broadcom demo service used by many
    // cheap smart plugs / bulbs (Tuya white-label OEM firmware).
    {0xFFF0, nullptr, 0xFFF1, nullptr, Kind::SmartBulb, "Generic FFF0/FFF1 (Tuya-family)"},

    // FFB0/FFB1 - MagicHue / MagicLight BLE smart bulbs, single write
    // for color and brightness.
    {0xFFB0, nullptr, 0xFFB1, nullptr, Kind::SmartBulb, "MagicHue/MagicLight bulb"},

    // ProximaCentauri / smart lock 0xFEE7 (SwitchBot lock has a control
    // handle here; some other DIY locks reuse it).
    {0xFEE7, nullptr, 0x0002, nullptr, Kind::SmartLock, "0xFEE7-family lock control"},
};
const size_t kCount = sizeof(kEntries) / sizeof(kEntries[0]);

const char* kindLabel(Kind k) {
    switch (k) {
        case Kind::SmartLock: return "LOCK";
        case Kind::SmartPlug: return "PLUG";
        case Kind::SmartBulb: return "BULB";
        case Kind::SmartBand: return "BAND";
        default: return "CTRL";
    }
}

}  // namespace ble_control_chars
