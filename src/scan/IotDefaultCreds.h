#pragma once

#include <cstddef>

// Small, well-known IoT default-credential table, keyed by a device
// fingerprint keyword. IotCredScanner matches `keyword` (case-insensitive
// substring) against a host's OUI vendor string and its service banners;
// on a match it tries exactly that device's documented factory default(s)
// on the given service. Entries with an empty keyword are the generic
// fallbacks tried on every device.
//
// This stays a "known factory defaults" list, NOT a wordlist — same line
// DefaultCredsDictionary draws (see CredDisclaimerScreen). The point is
// "is this camera/router still on its shipped password", not brute force.
struct IotCredential {
    const char* keyword;   // "" = generic; else vendor/banner substring, lowercase
    const char* service;   // "http" or "telnet"
    const char* user;
    const char* pass;
};

namespace IotDefaultCreds {

extern const IotCredential kEntries[];
extern const size_t kCount;

}  // namespace IotDefaultCreds
