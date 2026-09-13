#include "TimeSync.h"
#include <M5Unified.h>
#include <time.h>
#include <sys/time.h>

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

// UTC calendar date/time -> Unix epoch seconds, with no dependency on the
// process timezone (unlike mktime). Howard Hinnant's days_from_civil.
// Used only by provideExternalUtc() below.
time_t civilToEpochUtc(int y, unsigned m, unsigned d, unsigned hh, unsigned mm, unsigned ss) {
    y -= (m <= 2);
    const int era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (153u * (m + (m > 2 ? -3u : 9u)) + 2u) / 5u + d - 1u;
    const unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
    const long long days = (long long)era * 146097 + (long long)doe - 719468;
    return (time_t)(days * 86400LL + (long long)hh * 3600 + (long long)mm * 60 + (long long)ss);
}
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

void TimeSync::provideExternalUtc(uint16_t year, uint8_t month, uint8_t day, uint8_t hour, uint8_t minute,
                                  uint8_t second) {
    // Precedence: never overwrite a clock already at real time (NTP / RTC
    // seed / an earlier GPS reading). Safe to call ~1 Hz from the GPS task.
    if (isSynced()) return;
    if (month < 1 || month > 12 || day < 1 || day > 31 || hour > 23 || minute > 59 || second > 60) return;

    time_t epoch = civilToEpochUtc(year, month, day, hour, minute, second);
    if (epoch < kSyncedCutoff) return;  // implausible (before this project existed) - ignore

    struct timeval tv;
    tv.tv_sec = epoch;
    tv.tv_usec = 0;
    settimeofday(&tv, nullptr);
    // Not writing the RTC here: once the clock is real, syncRtcIfNeeded()
    // (called periodically from the UI task) persists it to an attached RTC
    // on its own, same path NTP already uses.
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
    // Sized well past the realistic "YYYY-MM-DD HH:MM:SS" output (19
    // chars + NUL) - GCC's -Wformat-truncation assumes each %d could be
    // a full int's worst case (up to 11 digits with sign) since tm_year
    // is a plain int with no compiler-visible range, so a buffer sized
    // to the realistic case still warns. Sized to fully cover that
    // worst case instead of narrowing the values (e.g. via modulo),
    // which would silently wrap an obviously-wrong huge/negative
    // tm_year into a plausible-looking one rather than surfacing it.
    char buf[96];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d", tmInfo.tm_year + 1900, tmInfo.tm_mon + 1,
             tmInfo.tm_mday, tmInfo.tm_hour, tmInfo.tm_min, tmInfo.tm_sec);
    return String(buf);
}

String TimeSync::nowTimeString() {
    if (!isSynced()) return "";
    time_t now = time(nullptr);
    struct tm tmInfo;
    gmtime_r(&now, &tmInfo);
    char buf[48];  // see nowString()'s comment on why this is well past "HH:MM:SS"'s 8 chars
    snprintf(buf, sizeof(buf), "%02d:%02d:%02d", tmInfo.tm_hour, tmInfo.tm_min, tmInfo.tm_sec);
    return String(buf);
}

String TimeSync::nowFilenameString() {
    if (!isSynced()) return "";
    time_t now = time(nullptr);
    struct tm tmInfo;
    gmtime_r(&now, &tmInfo);
    char buf[80];  // see nowString()'s comment on why this is well past "YYYYMMDD-HHMMSS"'s 15 chars
    snprintf(buf, sizeof(buf), "%04d%02d%02d-%02d%02d%02d", tmInfo.tm_year + 1900, tmInfo.tm_mon + 1, tmInfo.tm_mday,
             tmInfo.tm_hour, tmInfo.tm_min, tmInfo.tm_sec);
    return String(buf);
}
