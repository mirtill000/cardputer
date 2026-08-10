#pragma once

#include <Arduino.h>
#include <FS.h>
#include <cstdint>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

// Offline MAC-vendor lookup backed by data/oui/oui.bin on LittleFS (built
// by tools/gen_oui_db.py from a real IEEE MA-L registry snapshot — see
// data/oui/oui.csv and tools/extract_ieee_oui.py for provenance).
//
// Binary-searches directly on the filesystem via seek()s rather than
// loading the table into RAM: the full registry is ~1.2MB, which is
// fine for the 3.9MB LittleFS partition but not something we want to
// hold in SRAM on a board with no PSRAM.
class OuiDatabase {
public:
    bool begin(const char* path = "/oui.bin");
    bool lookup(const uint8_t mac[6], String& vendorOut) const;
    uint32_t recordCount() const { return _count; }
    bool isReady() const { return _ready; }

private:
    static constexpr uint8_t kVendorFieldLen = 32;
    static constexpr uint8_t kRecordSize = 3 + kVendorFieldLen;
    static constexpr uint8_t kHeaderSize = 8;

    mutable fs::File _file;
    mutable SemaphoreHandle_t _mutex = nullptr;
    uint32_t _count = 0;
    bool _ready = false;
};

extern OuiDatabase g_ouiDb;
