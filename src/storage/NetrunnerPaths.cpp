#include "NetrunnerPaths.h"
#include "../net/TimeSync.h"
#include "../net/WifiManager.h"

namespace {
String sanitizeForFilename(String s) {
    s.replace('/', '_');
    s.replace('\\', '_');
    s.replace(':', '_');
    s.replace(' ', '_');
    return s;
}
}  // namespace

String netrunner::reportBase(fs::FS& fs) {
    fs.mkdir("/netrunner");

    String stamp = TimeSync::isSynced() ? TimeSync::nowFilenameString() : ("uptime-" + String(millis() / 1000));

    String ssid = g_wifi.currentSsid();
    if (ssid.isEmpty()) ssid = "no-network";
    ssid = sanitizeForFilename(ssid);

    return "/netrunner/" + stamp + "_" + ssid;
}
