#pragma once

#include <Arduino.h>

// Wall-clock time via public NTP (SNTP) - this board has no battery-
// backed RTC, so every boot starts with only uptime (millis()) until
// this syncs, which needs WiFi. Uses ESP-IDF's built-in SNTP client via
// Arduino's configTime(), not a hand-rolled UDP client: unlike the rest
// of Fase 10's networking code, this doesn't need to parse an unfamiliar
// wire format, so there's no reason to reinvent it.
//
// UTC only - no timezone/DST handling. Every other timestamp already in
// this firmware (ScanHistory, the war-driving log) is UTC too, and one
// consistent timezone avoids a whole separate class of "wrong" instead
// of just adding a second one.
namespace TimeSync {

// Arms a background SNTP sync against `server`. Safe to call more than
// once (e.g. once at boot, again after each new WiFi connection) -
// configTime() just re-arms the same background sync, it doesn't
// restart or duplicate anything. Works even with no WiFi yet - the sync
// itself just won't complete until a connection exists.
void begin(const char* server = "pool.ntp.org");

// True once the SNTP client has actually gotten a reply and the system
// clock reflects real wall-clock time, not just time-since-epoch-0. The
// check: before a sync, ESP32's clock reads a small value near the Unix
// epoch (1970) - anything past a fixed recent cutoff is real.
bool isSynced();

// "YYYY-MM-DD HH:MM:SS" (UTC), or "" if not synced yet.
String nowString();

// "HH:MM:SS" (UTC) only - for compact status-bar display. "" if not
// synced yet.
String nowTimeString();

}  // namespace TimeSync
