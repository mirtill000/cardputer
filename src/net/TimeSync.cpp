#include "TimeSync.h"
#include <M5Unified.h>
#include <time.h>

namespace {
// 2020-09-13 00:00:00 UTC - comfortably before this project existed, so
// any epoch value past this is real synced time, not the ESP32's
// default post-boot clock (which reads near epoch 0).
constexpr time_t kSyncedCutoff = 1600000000;

// RTC writeback timing: don't write on the very first isSynced()==true
// tick — if that transition came from NTP (not the RTC seed itself),
// writing immediately is fine, but if it came from the RTC seed at boot,
// writing right back would just echo the chip's own value at itself with
// no benefit. There's no cheap way from here to tell which case just
// happened (see TimeSync.h - deliberately not chasing an SNTP completion
// callback for this), so instead: wait long enough that a WiFi-connected
// boot's NTP round trip has almost certainly already landed and
// corrected the clock (typically a few seconds), THEN do the first
// write - and keep re-writing periodically afterwards so a long session
// also keeps the RTC from drifting against whatever NTP keeps correcting
// it to.
constexpr uint32_t kRtcWriteGraceMs = 90000;       // 90s after first synced
constexpr uint32_t kRtcWriteIntervalMs = 1800000;  // then every 30 min

uint32_t g_syncedSinceMs = 0;   // millis() when isSynced() first went true this boot; 0 = not yet
uint32_t g_lastRtcWriteMs = 0;  // 0 = never written this session
}  // namespace

void TimeSync::begin(const char* server) {
    // Seed the system clock from the RTC BEFORE arming NTP, and only if
    // the clock isn't already real - begin() can legitimately be called
    // again later (e.g. after a fresh WiFi connection - see
    // WifiSetupScreen), at which point NTP may already have synced and
    // re-seeding from the RTC would be redundant (though harmless: the
    // RTC's own value should be close to the system clock by then too).
    //
    // RISK: M5.Rtc.setSystemTimeFromRtc() is M5Unified's own convenience
    // wrapper (utility/RTC_Class.hpp) - reads the chip, converts via
    // mktime() under a temporarily-forced GMT timezone, calls
    // settimeofday(). Not exercised on real Cardputer hardware by this
    // codebase yet (no RTC Unit in hand to test against - see README's
    // testing note); isEnabled() fails closed (false, this whole block
    // skipped) on every board without one attached, which is the
    // overwhelmingly common case for this hardware.
    if (rtcAvailable() && !isSynced()) {
        M5.Rtc.setSystemTimeFromRtc();
    }

    // gmtOffset_sec=0, daylightOffset_sec=0: UTC, no DST - see header.
    configTime(0, 0, server);
}

bool TimeSync::isSynced() {
    return time(nullptr) > kSyncedCutoff;
}

bool TimeSync::rtcAvailable() {
    // M5Unified probes the Grove port's I2C bus for a known RTC chip
    // address during M5Cardputer.begin() - see main.cpp's
    // cfg.external_rtc=true (default false; without it M5Unified never
    // looks, so a physically-attached unit would still read as absent
    // here). isEnabled() itself is cheap - a bool read of state already
    // captured at boot, not a fresh I2C transaction - safe to call as
    // often as this file does.
    return M5.Rtc.isEnabled();
}

bool TimeSync::rtcBatteryLow() {
    return rtcAvailable() && M5.Rtc.getVoltLow();
}

void TimeSync::syncRtcIfNeeded() {
    if (!rtcAvailable()) return;
    if (!isSynced()) {
        g_syncedSinceMs = 0;  // not synced (yet, or lost) - reset so a later sync waits out the grace period again
        return;
    }
    if (g_syncedSinceMs == 0) g_syncedSinceMs = millis();

    uint32_t now = millis();
    bool dueFirst = (g_lastRtcWriteMs == 0) && (now - g_syncedSinceMs > kRtcWriteGraceMs);
    bool duePeriodic = (g_lastRtcWriteMs != 0) && (now - g_lastRtcWriteMs > kRtcWriteIntervalMs);
    if (!dueFirst && !duePeriodic) return;

    time_t nowEpoch = time(nullptr);
    struct tm tmInfo;
    gmtime_r(&nowEpoch, &tmInfo);
    M5.Rtc.setDateTime(&tmInfo);  // RTC_Class::setDateTime(const tm*) overload - computes weekDay itself
    g_lastRtcWriteMs = now;
}

String TimeSync::nowString() {
    if (!isSynced()) return "";
    time_t now = time(nullptr);
    struct tm tmInfo;
    gmtime_r(&now, &tmInfo);
    char buf[24];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d", tmInfo.tm_year + 1900, tmInfo.tm_mon + 1,
             tmInfo.tm_mday, tmInfo.tm_hour, tmInfo.tm_min, tmInfo.tm_sec);
    return String(buf);
}

String TimeSync::nowTimeString() {
    if (!isSynced()) return "";
    time_t now = time(nullptr);
    struct tm tmInfo;
    gmtime_r(&now, &tmInfo);
    char buf[12];
    snprintf(buf, sizeof(buf), "%02d:%02d:%02d", tmInfo.tm_hour, tmInfo.tm_min, tmInfo.tm_sec);
    return String(buf);
}
