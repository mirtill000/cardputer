#pragma once

#include <FS.h>

// Generates a single self-contained HTML "kill chain" report from
// ScanManager's current host table onto any mounted filesystem (SD or
// LittleFS — same fs::FS& approach as ResultStore, which see for why the
// filesystem is passed in rather than hardcoded).
//
// The report is a human-readable companion to the machine-readable JSON/
// CSV export (both live under /netrunner/ — see storage/NetrunnerPaths.h):
// a cyberpunk-styled page with a summary, an
// "attack surface" section that ranks the most interesting findings
// (default-credential hits, plaintext services like telnet/ftp, exposed
// SMB, known-vulnerable banners), and a full host inventory table. It
// pulls only from the live host table plus the current WiFi context; it
// references (but does not re-aggregate) the artifact files other modules
// write to the card (wardrive.csv, handshakes/*.pcap, ...), listing the
// ones actually present so the report is a single index into a full
// assessment's output.
//
// All CSS is inlined and no external resources are referenced, so the
// file opens correctly straight off the card with no network access.
namespace ReportGenerator {

// Streams the report directly to `path` (no full in-memory document —
// same rationale as ResultStore). Returns false if the file can't be
// opened. Only alive hosts are included, matching ResultStore/the UI.
bool generate(fs::FS& fs, const char* path);

}  // namespace ReportGenerator
