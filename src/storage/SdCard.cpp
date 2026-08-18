#include "SdCard.h"
#include <LittleFS.h>
#include <SD.h>
#include <SPI.h>

namespace {

// M5Stack Cardputer / Cardputer ADV microSD slot pinout — a dedicated
// SPI bus (separate from the display's), per M5Stack's own Cardputer SD
// example sketch.
//
// UNVERIFIED IN THIS ENVIRONMENT: unlike every other pin/constant in
// this codebase, nothing else here already exercises the SD slot to
// cross-check these four numbers against, and this assistant has never
// compiled or run this code (see README's testing note) — this is the
// single least-confident set of magic numbers in the whole firmware. If
// begin() below always returns false even with a card inserted (check
// the serial log for "sdcard: no SD card detected"), this is the first
// place to correct.
constexpr int8_t kSdSckPin = 40;
constexpr int8_t kSdMisoPin = 39;
constexpr int8_t kSdMosiPin = 14;
constexpr int8_t kSdCsPin = 12;

SPIClass g_sdSpi(HSPI);
bool g_ready = false;

}  // namespace

bool sdcard::begin() {
    g_sdSpi.begin(kSdSckPin, kSdMisoPin, kSdMosiPin, kSdCsPin);
    // 25MHz: SD.begin()'s own conservative default. No reason to push
    // faster and risk signal integrity on a slot this codebase can't
    // verify the wiring of - see the pin comment above.
    g_ready = SD.begin(kSdCsPin, g_sdSpi, 25000000);
    if (!g_ready) {
        log_i("sdcard: no SD card detected (or mount failed) - exports/history will use LittleFS");
    } else {
        SD.mkdir("/history");
    }
    return g_ready;
}

bool sdcard::isReady() { return g_ready; }

fs::FS& sdcard::exportFs() {
    return g_ready ? static_cast<fs::FS&>(SD) : static_cast<fs::FS&>(LittleFS);
}

const char* sdcard::exportFsLabel() { return g_ready ? "SD" : "flash"; }
