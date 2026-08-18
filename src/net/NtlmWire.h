#pragma once

#include <Arduino.h>
#include <cstdint>
#include <vector>

// Minimal NTLM (MS-NLMP) message building/parsing — just enough to send
// a Type 1 NEGOTIATE_MESSAGE over HTTP and decode the domain/hostname
// info a server's Type 2 CHALLENGE_MESSAGE discloses in reply. Used by
// scan/NtlmHttpProbe for a pure information-disclosure check: this never
// completes the handshake (no Type 3 AUTHENTICATE_MESSAGE is ever built
// or sent), never supplies real credentials, and never captures or
// touches an NTLM hash — a server's Type 2 reply is sent to ANY client
// that negotiates NTLM, authenticated or not, precisely so the client
// can compute a response; reading its TargetInfo fields is the same
// category of recon as reading a banner, not credential theft.
//
// Verified against a real reference implementation before being used
// here: the Type 1 message's field layout was checked against Python's
// ntlm-auth library, and the Type 2 parsing logic (including the
// TargetInfo AV_PAIR walk and the UTF-16LE-to-ASCII decode) was tested
// against a realistic CHALLENGE_MESSAGE built with that same library's
// own TargetInfo/AvId classes (NetBIOS + DNS domain/computer name AV
// pairs, matching what a real Windows domain controller or IIS server
// sends). No live NTLM-speaking server or real ESP32 build involved
// (see README's testing note) — the parser fails closed (bounds-checked,
// returns false/empty on anything unexpected) for exactly that reason.
namespace ntlmwire {

// Builds a minimal, fixed 32-byte NTLM Type 1 NEGOTIATE_MESSAGE: no
// domain/workstation name supplied (both fields present but zero-
// length), NegotiateFlags = UNICODE | OEM | REQUEST_TARGET | NTLM |
// ALWAYS_SIGN | NEGOTIATE_TARGET_INFO (0x00808207) — the one flag that
// actually matters here is NEGOTIATE_TARGET_INFO: it's what asks the
// server to include the AV_PAIR block (domain/hostname info) in its
// Type 2 reply at all. Ready to base64-encode (see b64::encode) directly
// into an "Authorization: NTLM <...>" request header.
std::vector<uint8_t> buildType1Negotiate();

// Domain/hostname fields pulled from a Type 2 CHALLENGE_MESSAGE's
// TargetInfo AV_PAIRs (MS-NLMP §2.2.2.1) — each left as "" if the server
// didn't include that particular AV_PAIR (not every server sends all
// four).
struct Type2Info {
    String netbiosComputer;
    String netbiosDomain;
    String dnsComputer;
    String dnsDomain;
};

// Parses an NTLM Type 2 CHALLENGE_MESSAGE (the raw bytes, already
// base64-decoded — see b64::decode). Returns false if `buf` doesn't even
// have the NTLMSSP signature/message-type-2 header, or is too short for
// the fixed 48-byte header up through TargetInfoFields. A true return
// with every Type2Info field empty is possible too (message parses, but
// carries no TargetInfo — some server implementations omit it even when
// asked) — that's a valid, if less useful, result, not a parse failure.
bool parseType2Challenge(const uint8_t* buf, size_t len, Type2Info& out);

}  // namespace ntlmwire
