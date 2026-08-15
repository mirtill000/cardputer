#pragma once

#include <Arduino.h>

// Wall-clock time via public NTP (SNTP), now with an optional assist from
// a battery-backed RTC module (M5Stack "RTC Unit"/Mini RTC, BM8563-based)
// plugged into the Grove port — see main.cpp's cfg.external_rtc=true.
//
// This board (Cardputer/Cardputer ADV) has NO onboard RTC chip — unlike
// M5Stack Core2/CoreS3, which do — so without an external unit attached,
// every boot still starts with only uptime (millis()) until NTP syncs,
// which needs WiFi, exactly as before this module existed. WITH one
// attached: begin() seeds the system clock from it immediately at boot
// (real wall-clock time available before WiFi even connects), and every
// successful NTP sync writes the fresh time back to the RTC so it stays
// accurate — and keeps counting through power-off — between sessions.
// Detection is fully automatic (M5Unified's board bring-up probes the
// Grove port's I2C bus for a known RTC chip address); nothing in this
// module or its callers needs to know whether a unit is physically
// attached, everything below just degrades to "no RTC" if isEnabled()
// ever comes back false, same as this firmware treats every other
// optional peripheral (SD card, IMU).
//
// UTC only - no timezone/DST handling. Every other timestamp already in
// this firmware (ScanHistory, the war-driving log) is UTC too, and one
// consistent timezone avoids a whole separate class of "wrong" instead
// of just adding a second one. The RTC chip itself is also kept in UTC
// (M5Unified's rtc_time_t has no timezone concept of its own — it just
// stores whatever wall-clock fields it's given), so no conversion happens
// on either side of the seed/writeback.
namespace TimeSync {

// Arms a background SNTP sync against `server`, and — if a battery-backed
// RTC is present — seeds the system clock from it first so there's real
// wall-clock time even before WiFi connects. Safe to call more than once
// (e.g. once at boot, again after each new WiFi connection) - configTime()
// just re-arms the same background sync, it doesn't restart or duplicate
// anything, and the RTC seed only ever runs once (see isSynced() - once
// the clock reads a real time, re-seeding from the RTC would be a no-op
// anyway). Works even with no WiFi yet - the NTP sync itself just won't
// complete until a connection exists.
void begin(const char* server = "pool.ntp.org");

// True once either the RTC seed or the SNTP client has actually set the
// system clock to real wall-clock time, not just time-since-epoch-0. The
// check: before either, ESP32's clock reads a small value near the Unix
// epoch (1970) - anything past a fixed recent cutoff is real.
bool isSynced();

// True if a battery-backed RTC module was detected on the Grove port's
// I2C bus at boot (M5Unified's external_rtc auto-probe - see main.cpp).
// False on every Cardputer/Cardputer ADV with nothing plugged in there,
// which is the expected common case, not an error.
bool rtcAvailable();

// True if the RTC reports its backup battery is low/depleted (only
// meaningful when rtcAvailable() is true) - the chip keeps ticking off
// USB power but will lose the time the moment both USB and its battery
// are gone. Surfaced in DIAGNOSTICS, not polled/alerted on elsewhere -
// this is a "replace the coin cell eventually" signal, not urgent.
bool rtcBatteryLow();

// Call periodically (e.g. once every few seconds from the UI task's main
// loop, alongside the existing battery-level check) so a fresh NTP sync
// gets written back to the RTC once, promptly, without every caller of
// isSynced()/nowString() needing to know this bookkeeping exists. No-op
// (cheap: one bool check) if there's no RTC, or the writeback already
// happened this session, or NTP hasn't synced yet.
void syncRtcIfNeeded();

// "YYYY-MM-DD HH:MM:SS" (UTC), or "" if not synced yet.
String nowString();

// "HH:MM:SS" (UTC) only - for compact status-bar display. "" if not
// synced yet.
String nowTimeString();

// "YYYYMMDD-HHMMSS" (UTC) - filesystem-safe (no ':'/' ' the way
// nowString()'s human-readable form has), for building timestamped
// filenames (see storage/NetrunnerPaths.h). "" if not synced yet, same
// convention as nowString()/nowTimeString() - callers fall back to an
// uptime-based stamp themselves, same as WardrivingManager's CSV log
// already does for its own timestamp column.
String nowFilenameString();

}  // namespace TimeSync
