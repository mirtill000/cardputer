#pragma once

#include <Arduino.h>
#include <cstddef>
#include <cstdint>

// Minimal Base64 encoder — just enough for building an HTTP
// "Authorization: Basic <...>" header (CredAuditManager's only use).
// Algorithm/output verified against known test vectors (RFC 4648 +
// "admin:admin" -> "YWRtaW46YWRtaW4=") in a standalone host-side test
// before being used anywhere network-facing — see git history.
//
// Named b64, not base64: arduino-esp32's own core ships a
// cores/esp32/base64.cpp with an identically-shaped `base64::encode`,
// always present in libFrameworkArduino.a but only actually pulled into
// the link once something else in the build references it (archive
// members link lazily) — which net/OtaUpdater.cpp's use of HTTPClient
// started doing, surfacing a real "multiple definition of
// base64::encode" link error the first time this project reached a
// full link with that combination. Renaming ours sidesteps the
// collision entirely rather than trying to reuse or shadow the core's.
namespace b64 {

String encode(const uint8_t* data, size_t len);

inline String encode(const String& s) {
    return encode(reinterpret_cast<const uint8_t*>(s.c_str()), s.length());
}

}  // namespace b64
