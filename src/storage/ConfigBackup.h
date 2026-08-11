#pragma once

#include <FS.h>

// Backs up/restores AppConfig, saved WiFi networks (SSID+password), and
// the war-driving allowlist as a single JSON file.
//
// SD card ONLY, deliberately never falling back to LittleFS the way
// export/history code elsewhere does (see storage/SdCard.h): the whole
// point of this is surviving a full-chip `pio run -t erase` (the OTA
// partition table's own recommended recovery step - see partitions.csv
// and README's Fase 10), which wipes LittleFS right along with NVS on
// the same flash chip. A backup written there would be erased by the
// very operation it's meant to help recover from.
//
// Includes WiFi passwords in plaintext - same trust model as every
// other credential this firmware stores (NVS is already unencrypted,
// see WifiManager.h's own note): anyone with physical access to pull
// the SD card already has the same access to the NVS chip on the same
// board. See README for the fuller note.
namespace ConfigBackup {

bool backup(fs::FS& fs, const char* path);

// Clears existing saved WiFi networks and war-driving allowlist entries
// before applying the backup's, so the result matches the backup
// exactly rather than merging with whatever was already saved.
bool restore(fs::FS& fs, const char* path);

}  // namespace ConfigBackup
