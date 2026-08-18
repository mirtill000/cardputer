#pragma once

#include <Arduino.h>

// A small, hand-picked table of banner substrings that identify a
// specific, well-documented, historically significant vulnerability or
// end-of-life software build — NOT a CVE database, and NOT a fuzzy
// version-range parser. Every entry here is an exact, unambiguous
// string a real service actually sends (see VulnSignatures.cpp for each
// one's provenance) matched with a plain substring search. That
// conservatism is deliberate: a false "vulnerable" call on an audit
// tool people might actually act on is worse than missing an obscure
// case this small table doesn't cover.
namespace VulnSignatures {

// Returns true and fills noteOut with a short human-readable
// description if `banner` contains one of the known signatures; false
// (noteOut left untouched) otherwise.
bool check(const String& banner, String& noteOut);

}  // namespace VulnSignatures
