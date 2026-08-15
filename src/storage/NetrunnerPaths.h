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
// Deliberately NOT used by WardrivingManager's own per-AP excursion
// exports (`/wardrive/scans/<ssid>_<bssid>.json/.csv`) — that mechanism
// already has its own, separate, SSID+BSSID-keyed namespace by design
// (see WardrivingManager.cpp: "own namespace, separate from the user's
// own SCAN HISTORY"), and folding it in here would blur a distinction
// the codebase draws on purpose. This covers the NETWORK SCAN/AUTO
// ASSESS surfaces only.
namespace netrunner {

// Ensures /netrunner exists on `fs` (mkdir() is a harmless no-op if it's
// already there) and returns "/netrunner/<TIMESTAMP>_<ssid>" — no
// extension, same "build one shared base, append your own .json/.csv/
// .html" pattern WardrivingManager::handleOpenAllowlistedAp already uses
// for its own export basenames (and for the same reason: a JSON/CSV/HTML
// trio written from one call site should share one timestamp, not each
// separately-computed second potentially landing in a different one).
//  - TIMESTAMP: TimeSync::nowFilenameString() ("YYYYMMDD-HHMMSS", UTC) if
//    synced (real time - possibly RTC-seeded even before WiFi, see
//    net/TimeSync.h - or NTP), else "uptime-<seconds>". Always
//    filesystem-safe, always sorts chronologically as plain text either
//    way.
//  - ssid: the currently-connected network's SSID, sanitized ('/', '\\',
//    ':', ' ' replaced with '_' - the same characters WardrivingManager
//    already strips for its own per-AP filenames) so it can never
//    produce an invalid path component. "no-network" if not connected
//    (shouldn't normally happen - every caller of this only runs after a
//    scan, which needs WiFi - but this fails closed to a valid filename
//    rather than an empty/malformed one regardless).
String reportBase(fs::FS& fs);

}  // namespace netrunner
