#pragma once

#include <Arduino.h>
#include <cstddef>
#include <cstdint>

// Minimal Base64 encoder — just enough for building an HTTP
// "Authorization: Basic <...>" header (CredAuditManager's only use).
// Algorithm/output verified against known test vectors (RFC 4648 +
// "admin:admin" -> "YWRtaW46YWRtaW4=") in a standalone host-side test
// before being used anywhere network-facing — see git history.
namespace base64 {

String encode(const uint8_t* data, size_t len);

inline String encode(const String& s) {
    return encode(reinterpret_cast<const uint8_t*>(s.c_str()), s.length());
}

}  // namespace base64
