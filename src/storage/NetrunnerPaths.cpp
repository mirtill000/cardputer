#include "NetrunnerPaths.h"
#include "../net/TimeSync.h"

namespace {
// Whitelist sanitizer: keep only characters that are safe in a filename
// on both LittleFS and the SD card's FAT/VFAT, mapping everything else
// to '_'. An SSID logged off the air (used as the label for war-driving
// excursion exports and SENTINEL pcaps) is untrusted and can contain
// path separators (/ \ :), the FAT-reserved set (* ? < > | "), spaces,
// or control bytes - any of which would otherwise make fs.open() fail
// silently and drop that run's report/export/pcap.
String sanitizeForFilename(const String& s) {
    String out;
    out.reserve(s.length());
    for (size_t i = 0; i < s.length(); i++) {
        char c = s[i];
        bool safe = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' ||
                    c == '_' || c == '.';
        out += safe ? c : '_';
    }
    return out;
}
}  // namespace

String netrunner::reportBase(fs::FS& fs, const String& label) {
    fs.mkdir("/netrunner");

    String stamp = TimeSync::isSynced() ? TimeSync::nowFilenameString() : ("uptime-" + String(millis() / 1000));
    String safeLabel = label.length() ? sanitizeForFilename(label) : String("no-network");

    return "/netrunner/" + stamp + "_" + safeLabel;
}
