#pragma once

#include <FS.h>

// Exports ScanManager's current host table as JSON or CSV onto any
// mounted filesystem. Takes an fs::FS& (LittleFS or SD both implement
// it) rather than hardcoding LittleFS, so the same export logic works
// for either — see README for why SD isn't wired up with a default
// chip-select pin here (the Cardputer ADV's microSD CS pin isn't
// something this codebase could verify without the hardware in hand).
//
// Both functions stream directly to the file rather than building an
// in-memory document first: with no PSRAM and up to hundreds of hosts
// each carrying a port list, buffering the whole export in RAM before
// writing it isn't a risk worth taking for what's fundamentally a
// straight-through serialization.
namespace ResultStore {

// Only exports hosts with alive == true (see HostListScreen — the rest
// of the UI already treats "alive" as the definition of "a real
// result"; exporting the full, mostly-empty probe table would just
// make the file bigger without adding information).
bool exportJson(fs::FS& fs, const char* path);
bool exportCsv(fs::FS& fs, const char* path);

}  // namespace ResultStore
