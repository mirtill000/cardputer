#pragma once

#include <cstdint>

// Small, curated BLE Company ID → vendor lookup. Not the whole Bluetooth
// SIG assigned-numbers registry (thousands of entries, most never seen on
// consumer LANs) — just the IDs BluetoothManager needs to identify at a
// glance: the big-consumer platforms whose advertising data
// BluetoothManager parses (Apple/Microsoft for Continuity/Swift Pair,
// Samsung/Google for their tracker + Fast Pair schemes) plus the vendors
// commonly seen in home/office BLE inventory (Nordic/Espressif for
// dev-kit gear, Fitbit/Garmin for wearables, ...).
//
// Extending this table is cheap: it's a linear scan against ~30 entries,
// on lookup once per newly-seen device. Not perf-critical.
namespace ble_company_ids {

const char* lookup(uint16_t companyId);

}  // namespace ble_company_ids
