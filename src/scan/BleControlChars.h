#pragma once

#include <cstdint>
#include <cstddef>

// Small, curated table of known "control channel" characteristics for
// consumer smart-home BLE devices (feature #8). BleGattClient looks up
// (service UUID, characteristic UUID) pairs it discovers during a GATT
// walk against this table; if a match hits AND the characteristic is
// writable, it flags "known control vector present". This is DETECTION
// ONLY - BleGattClient NEVER writes to these characteristics, because
// writing could physically actuate something (unlock a door, toggle a
// plug, turn on a heater). Same line the IoT default-credentials tool
// draws (Fase 51): "known vector present, verify manually" is the whole
// finding.
//
// The kind (SmartLock/SmartPlug/SmartBulb/SmartBand) is used only for
// the UI label so the risk severity is legible - a writable control on
// a smart lock is more alarming than one on a light bulb.
//
// UUIDs are 16-bit where the Bluetooth SIG assigned one; 128-bit
// vendor-specific UUIDs are represented by the last-16-bit prefix and
// the rest is compared as a full 128-bit UUID inside the .cpp file.
namespace ble_control_chars {

enum class Kind : uint8_t {
    SmartLock,
    SmartPlug,
    SmartBulb,
    SmartBand,
    Vendor,   // known vendor control (unclassified physical action)
};

struct Entry {
    // Either serviceUuid16 != 0 (16-bit) OR serviceUuid128 non-null.
    uint16_t serviceUuid16;
    const char* serviceUuid128;
    // Same rule for the characteristic.
    uint16_t charUuid16;
    const char* charUuid128;
    Kind kind;
    const char* label;
};

extern const Entry kEntries[];
extern const size_t kCount;

const char* kindLabel(Kind k);

}  // namespace ble_control_chars
