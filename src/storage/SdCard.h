#pragma once

#include <FS.h>

// Optional microSD card support: mounted at boot if a card is present,
// silently unavailable (not an error state) if not — a handheld tool
// without a card in the slot is the normal case, not a fault.
//
// Export/history code should always go through exportFs()/exportFsLabel()
// rather than hardcoding LittleFS, so results land on the removable card
// (easy to pull and read on a PC) whenever one is inserted, and fall back
// to internal flash otherwise.
namespace sdcard {

// Safe to call even with no card inserted - returns false, logs once,
// and every other function below degrades to the LittleFS fallback.
bool begin();
bool isReady();

fs::FS& exportFs();
const char* exportFsLabel();  // "SD" or "flash", for status lines

}  // namespace sdcard
