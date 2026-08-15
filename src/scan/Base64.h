#pragma once

#include <Arduino.h>
#include <cstddef>
#include <cstdint>
#include <vector>

// Minimal Base64 encoder/decoder. Encode started as just enough for
// building an HTTP "Authorization: Basic <...>" header (CredAuditManager's
// only use); decode was added for scan/NtlmHttpProbe, which needs to pull
// the binary NTLM Type 2 (Challenge) message back out of a server's
// "WWW-Authenticate: NTLM <base64>" response header. Algorithm/output
// verified against known test vectors (RFC 4648 + "admin:admin" ->
// "YWRtaW46YWRtaW4=" for encode; round-tripped through Python's base64
// module across empty/every padding length/random binary payloads up to
// 166 bytes — an NTLM Type 2 message's typical size — for decode) in a
// standalone host-side test before being used anywhere network-facing —
// see git history.
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

// Decodes standard (RFC 4648 §4) Base64 text, '+'/'/' alphabet. Stops at
// the first '=' padding character (whatever was decoded up to that point
// is the final result) and silently skips any other character outside
// the Base64 alphabet (whitespace, line breaks) rather than failing the
// whole decode — real-world "WWW-Authenticate: NTLM <...>" header values
// are a single unbroken token in practice, but skipping rather than
// rejecting stray characters costs nothing and avoids depending on that.
std::vector<uint8_t> decode(const String& in);

}  // namespace b64
