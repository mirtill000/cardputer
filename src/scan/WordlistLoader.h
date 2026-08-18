#pragma once

#include <Arduino.h>
#include <cstddef>
#include <vector>

// Loads a newline-separated wordlist from LittleFS (e.g.
// data/creds/users.txt, data/creds/passwords.txt — plain text, one
// entry per line, blank lines and lines starting with '#' skipped).
// There's no on-device file upload (no SD chip-select pin verified for
// this board — see README — and no web server), so customizing these
// means editing the text file in the repo and re-running
// `pio run -t uploadfs`, the same workflow as the OUI database.
//
// Capped at maxEntries: this bounds both the RAM footprint (loaded
// fully in-memory, unlike the OUI database's on-flash binary search)
// and how long a brute-force run can possibly take, since
// CredAuditManager tries every user x password combination.
namespace WordlistLoader {

std::vector<String> load(const char* path, size_t maxEntries);

}  // namespace WordlistLoader
