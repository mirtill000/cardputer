#pragma once

#include <Arduino.h>
#include <FS.h>

// Shared filename convention for every "save a snapshot of the current
// scan" action in this firmware (NETWORK SCAN's JSON/CSV export and HTML
// report, AUTO ASSESS's HTML report): one folder, one naming scheme, so
// every run's report lands in its own file instead of overwriting a
// single fixed one (export.json/export.csv/report.html, this
// firmware's behavior before this existed). "netrunner" matches the UI
// restyle mockup name this app has followed since Fase 6/9 ("NETRUNNER")
// — not an arbitrary folder name.
//
// Used by every scan-report surface in the firmware, including
// WardrivingManager's per-AP excursion exports and its continuous
// wardrive.csv sighting log (moved here in Fase 29 — previously their own
// separate /wardrive/ namespace) — one shared report folder/naming scheme
// for anything a user would want to pull off the card and look at later.
namespace netrunner {

// Ensures /netrunner exists on `fs` (mkdir() is a harmless no-op if it's
// already there) and returns "/netrunner/<TIMESTAMP>_<label>" — no
// extension, "build one shared base, append your own .json/.csv/.html"
// so a JSON/CSV/HTML trio written from one call site shares one
// timestamp, not each separately-computed second potentially landing in
// a different one.
//  - TIMESTAMP: TimeSync::nowFilenameString() ("YYYYMMDD-HHMMSS", UTC) if
//    synced (real time - possibly RTC-seeded even before WiFi, see
//    net/TimeSync.h - or NTP), else "uptime-<seconds>". Always
//    filesystem-safe, always sorts chronologically as plain text either
//    way.
//  - label: identifies what this report is about - sanitized ('/', '\\',
//    ':', ' ' replaced with '_') so it can never produce an invalid path
//    component or an unintended subdirectory. Callers pass whatever
//    makes sense for them: NETWORK SCAN/AUTO ASSESS pass
//    `g_wifi.currentSsid()` (the network they're actually scanning);
//    WardrivingManager passes an explicit `ssid + "_" + bssid` for a
//    specific AP excursion rather than relying on still being connected
//    to it by the time this is called. "" resolves to "no-network"
//    rather than an empty/malformed path component.
String reportBase(fs::FS& fs, const String& label);

}  // namespace netrunner
