#include "NetrunnerPaths.h"
#include "../net/TimeSync.h"

namespace {
String sanitizeForFilename(String s) {
    s.replace('/', '_');
    s.replace('\\', '_');
    s.replace(':', '_');
    s.replace(' ', '_');
    return s;
}
}  // namespace

String netrunner::reportBase(fs::FS& fs, const String& label) {
    fs.mkdir("/netrunner");

    String stamp = TimeSync::isSynced() ? TimeSync::nowFilenameString() : ("uptime-" + String(millis() / 1000));
    String safeLabel = label.length() ? sanitizeForFilename(label) : String("no-network");

    return "/netrunner/" + stamp + "_" + safeLabel;
}
