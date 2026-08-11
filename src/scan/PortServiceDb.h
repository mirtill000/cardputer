#pragma once

#include <Arduino.h>
#include <FS.h>
#include <cstdint>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

// Offline TCP/UDP port -> service name lookup, backed by
// data/ports/services.bin on LittleFS (built by
// tools/gen_port_services_db.py from an independently-extracted fact
// list — see tools/extract_port_services.py for why this is not just
// nmap-services shipped as-is; short version: licensing). ~12,000
// entries, real IANA-registered service names, not the ~15-port
// hardcoded switch BannerGrabber used before this.
//
// Same binary-search-on-flash design as OuiDatabase, for the same
// reason: ~300KB is fine for the LittleFS partition but not something
// to hold in SRAM on a board with no PSRAM.
class PortServiceDb {
public:
    // Default path matches where PlatformIO's uploadfs puts
    // data/ports/services.bin: LittleFS preserves the subdirectory, so
    // it's "/ports/services.bin" at runtime — see the OuiDatabase
    // header for a case where getting this wrong shipped unnoticed for
    // a while (a failed begin() degrades silently, no crash).
    bool begin(const char* path = "/ports/services.bin");

    // udp: false = tcp, true = udp (matches the on-disk encoding).
    bool lookup(uint16_t port, bool udp, String& serviceOut) const;

    uint32_t recordCount() const { return _count; }
    bool isReady() const { return _ready; }

private:
    static constexpr uint8_t kNameFieldLen = 22;
    static constexpr uint8_t kRecordSize = 2 + 1 + kNameFieldLen;
    static constexpr uint8_t kHeaderSize = 8;

    mutable fs::File _file;
    mutable SemaphoreHandle_t _mutex = nullptr;
    uint32_t _count = 0;
    bool _ready = false;
};

extern PortServiceDb g_portServiceDb;
