#include "BleCompanyIds.h"

namespace ble_company_ids {

namespace {
struct Entry {
    uint16_t id;
    const char* name;
};

// Bluetooth SIG assigned numbers — see
// https://www.bluetooth.com/specifications/assigned-numbers/
// Curated for consumer-LAN visibility; keep short (Fase 54).
constexpr Entry kEntries[] = {
    {0x0006, "Microsoft"},
    {0x000F, "Broadcom"},
    {0x001D, "Qualcomm"},
    {0x0059, "Nordic Semiconductor"},
    {0x0075, "Samsung"},
    {0x0087, "Garmin"},
    {0x00A8, "Sony"},
    {0x00E0, "Google"},
    {0x0131, "Cypress"},
    {0x0157, "Anhui Huami (Xiaomi wearable)"},
    {0x0171, "Amazon"},
    {0x0181, "Nintendo"},
    {0x01A9, "Fitbit"},
    {0x0201, "Bose"},
    {0x0204, "Insta360"},
    {0x0224, "Logitech"},
    {0x0225, "Anker"},
    {0x022B, "Beats (Apple)"},
    {0x0234, "TP-Link"},
    {0x0263, "GoPro"},
    {0x02A0, "JBL"},
    {0x02E0, "Xiaomi"},
    {0x02FF, "Silicon Labs"},
    {0x0397, "Ultimate Ears"},
    {0x038F, "Realtek"},
    {0x03DA, "Ring"},
    {0x048C, "MediaTek"},
    {0x0499, "Ruuvi"},
    {0x004C, "Apple"},
    {0x0157, "Xiaomi (Huami)"},
    {0x0157, "Huami"},
    // ESP32 default in some IDFs; useful when this device sees another
    // ESP-based device advertising nearby.
    {0x02E5, "Espressif"},
};
constexpr size_t kCount = sizeof(kEntries) / sizeof(kEntries[0]);
}  // namespace

const char* lookup(uint16_t companyId) {
    for (size_t i = 0; i < kCount; i++) {
        if (kEntries[i].id == companyId) return kEntries[i].name;
    }
    return nullptr;  // caller falls back to "co:XXXX" hex
}

}  // namespace ble_company_ids
