#pragma once

#include <Arduino.h>
#include <cstdint>
#include <vector>

// Minimal LDAPv3 (RFC 4511) message building/parsing — just enough for
// an anonymous simple BindRequest and a base-scope SearchRequest against
// the rootDSE ("" as baseObject), the two operations scan/LdapProbe
// needs. LDAP's wire format is BER (ASN.1 Basic Encoding Rules,
// RFC 4511 §5.1 / X.690) — a real binary protocol, same category of
// "tricky enough to get wrong" as net/DnsWire.h's name compression, so
// it's centralized here rather than inlined into LdapProbe.cpp.
//
// Verified against a real ASN.1 encoder before being used here: every
// byte sequence buildAnonymousBind()/buildRootDseSearch() produce, and
// the parsing logic parseBindResponse()/parseSearchResultEntry() use,
// were checked against messages built with Python's pyasn1 + ldap3's own
// RFC 4511 ASN.1 module (ldap3.protocol.rfc4511) — a real, standards-
// compliant LDAP client library, not a hand-derived guess — including a
// realistic multi-attribute, multi-value rootDSE reply that exercises
// BER's long-form length encoding (>127-byte elements). Still: no live
// LDAP server or real ESP32 build involved (see README's testing note) —
// a real Active Directory/OpenLDAP server's exact reply shape could
// still differ in ways this couldn't catch (e.g. a resultCode this
// wasn't tested against, or controls/referrals attached to the
// response) — the parser fails closed (bounds-checked, returns
// false/empty on anything unexpected) for exactly that reason.
namespace ldapwire {

// Builds a complete anonymous simple BindRequest LDAPMessage
// (messageID=1, version=3, empty name, empty simple-auth password —
// RFC 4511 §4.2's "unauthenticated bind" shape when name is present, or
// true anonymous when name is also empty, which is what this sends).
std::vector<uint8_t> buildAnonymousBind();

// Builds a complete SearchRequest LDAPMessage (messageID=2) for the
// rootDSE: baseObject="", scope=baseObject(0), derefAliases=never(0),
// sizeLimit=0, timeLimit=5s, typesOnly=FALSE, filter=(objectClass=*),
// attributes=[namingContexts, defaultNamingContext, dnsHostName] — the
// three rootDSE attributes most useful for an unauthenticated recon
// check (naming contexts show the domain DN even when nothing else is
// readable; dnsHostName often reveals the DC's real hostname).
std::vector<uint8_t> buildRootDseSearch();

// Parses a BindResponse LDAPMessage. Returns false if `buf` doesn't
// parse as one at all (wrong shape, truncated, wrong op tag) — NOT the
// same as a rejected bind, which parses fine and sets `success=false`.
bool parseBindResponse(const uint8_t* buf, size_t len, bool& success);

// Parses ONE SearchResultEntry LDAPMessage and fills in any of
// `wantedAttrs` that were present, taking only the FIRST value of each
// (rootDSE attributes like namingContexts are often multi-valued — this
// is a best-effort single-line summary, not an exhaustive dump; see
// scan/LdapProbe.h). `out` size/order matches `wantedAttrs`; entries for
// attributes not found in this message are left untouched (caller
// should pre-size/clear `out` as needed). Returns false if `buf` isn't a
// SearchResultEntry at all (e.g. it's actually the SearchResultDone that
// follows every entry — callers should try this first and fall back to
// treating a `false` return as "no more entries").
bool parseSearchResultEntry(const uint8_t* buf, size_t len, const std::vector<String>& wantedAttrs,
                             std::vector<String>& out);

// True if `buf` parses as a SearchResultDone LDAPMessage (regardless of
// its resultCode) — the marker that no more SearchResultEntry messages
// will follow on this connection for this search.
bool isSearchResultDone(const uint8_t* buf, size_t len);

}  // namespace ldapwire
