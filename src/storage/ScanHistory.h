#pragma once

#include <FS.h>
#include <IPAddress.h>
#include <vector>
#include "../core/Types.h"

// Persists a lightweight snapshot of each finished discovery scan (and
// the last known open-port set of each individually port-scanned host)
// to /history/ on whichever filesystem the caller passes in (see
// storage/SdCard.h — SD when present, LittleFS otherwise). This is what
// backs the SCAN HISTORY screen and the "new host" / "newly open port"
// diffing HostListScreen and PortScanScreen show after a scan.
//
// Deliberately its own small JSON files rather than reusing
// ResultStore's export format: ResultStore is a one-way, streaming,
// human-facing export (JSON/CSV, overwritten each time at a fixed
// path); this module needs to read its own data back, so it uses
// ArduinoJson's in-memory document (already a declared dependency, just
// not exercised elsewhere in the codebase yet) instead of hand-rolled
// streaming writes.
namespace ScanHistory {

struct HistoryEntry {
    String filename;    // full path, e.g. "/history/scan_00007.json"
    uint32_t seq = 0;
    size_t hostCount = 0;
};

struct HistoryHostSnapshot {
    IPAddress ip;
    String mac;
    String hostname;
    String vendor;
    String deviceClass;  // label string, e.g. "ROUTER" - see deviceClassLabel()
    String risk;         // "ok" / "warning" / "critical"
};

// Only kMaxEntries most recent scan snapshots are kept - older ones are
// pruned automatically as new ones are saved, so history can't grow
// without bound over the device's lifetime.
constexpr uint8_t kMaxEntries = 20;

// Snapshots every currently-alive host from g_scanManager. Called once
// per finished discovery scan. Returns false only on an FS write error
// (a full/missing filesystem) - never on "nothing to save".
bool saveSnapshot(fs::FS& fs);

// Lists saved snapshots, newest first (entries[0] is the most recent).
size_t listEntries(fs::FS& fs, std::vector<HistoryEntry>& out);

// Loads one snapshot's host list, e.g. for the SCAN HISTORY detail view
// or for diffNewHosts() below.
bool loadEntry(fs::FS& fs, const String& filename, std::vector<HistoryHostSnapshot>& out);

// Compares the scan just saved by saveSnapshot() against the one before
// it and returns the IPs that are newly alive - i.e. weren't present in
// the previous snapshot. Empty (and returns 0) if there's no previous
// snapshot to compare against yet (the very first scan ever).
size_t diffNewHosts(fs::FS& fs, std::vector<IPAddress>& newHostsOut);

// Compares `ports` (a host's just-finished TCP+UDP port scan results)
// against the open-port set saved the last time this same host was
// port-scanned, setting isNewPort on any that weren't open before, then
// overwrites that per-host snapshot with the current set for next time.
// Leaves every isNewPort false (nothing to compare against) the first
// time a given host is ever port-scanned. Keys purely by port number,
// not (port, proto) - a TCP and a UDP result sharing the same port
// number are indistinguishable to this diff, a deliberately accepted
// imprecision for a feature that's already best-effort by nature (see
// UdpProbe.h).
void diffAndSavePorts(fs::FS& fs, const IPAddress& target, std::vector<PortResult>& ports);

}  // namespace ScanHistory
