#include "TimeSync.h"
#include <time.h>

namespace {
// 2020-09-13 00:00:00 UTC - comfortably before this project existed, so
// any epoch value past this is real synced time, not the ESP32's
// default post-boot clock (which reads near epoch 0).
constexpr time_t kSyncedCutoff = 1600000000;
}  // namespace

void TimeSync::begin(const char* server) {
    // gmtOffset_sec=0, daylightOffset_sec=0: UTC, no DST - see header.
    configTime(0, 0, server);
}

bool TimeSync::isSynced() {
    return time(nullptr) > kSyncedCutoff;
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
