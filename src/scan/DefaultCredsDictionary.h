#pragma once

#include <cstddef>

struct DefaultCredential {
    const char* user;
    const char* pass;
};

// Small, fixed, well-known default-credential dictionary — deliberately
// NOT exhaustive and NOT vendor-specific. This is what keeps the
// credential audit a "check known defaults" tool rather than a
// brute-forcer: CredAuditManager tries exactly these pairs, in this
// order, and nothing else, ever. Extend with more well-known generic
// defaults if needed, but resist the urge to turn this into a wordlist
// — see the disclaimer in ui/screens/CredDisclaimerScreen.cpp for why
// that line matters.
namespace DefaultCredsDictionary {

extern const DefaultCredential kEntries[];
extern const size_t kCount;

}  // namespace DefaultCredsDictionary
