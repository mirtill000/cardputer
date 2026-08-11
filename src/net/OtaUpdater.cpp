#include "OtaUpdater.h"
#include <HTTPClient.h>
#include <Update.h>
#include <WiFiClient.h>

bool OtaUpdater::run(const String& url, String& errorOut) {
    if (!url.startsWith("http://")) {
        // Plain HTTP only, deliberately: HTTPS would mean this MCU
        // needs to either hold a CA bundle for whatever server the user
        // points it at, or skip certificate verification and defeat the
        // point of HTTPS entirely. Out of scope for what this is meant
        // to be - flashing a .bin from a machine on the same LAN (e.g.
        // `python3 -m http.server` in the build output directory) - not
        // a general-purpose secure updater. See README.
        errorOut = "only http:// URLs are supported (not https)";
        return false;
    }

    HTTPClient http;
    if (!http.begin(url)) {
        errorOut = "could not start HTTP request - malformed URL?";
        return false;
    }

    int code = http.GET();
    if (code != HTTP_CODE_OK) {
        errorOut = "HTTP " + String(code);
        http.end();
        return false;
    }

    int len = http.getSize();
    if (len <= 0) {
        errorOut = "server didn't report a content length";
        http.end();
        return false;
    }

    if (!Update.begin((size_t)len)) {
        errorOut = String("won't fit in the inactive OTA slot: ") + Update.errorString();
        http.end();
        return false;
    }

    WiFiClient* stream = http.getStreamPtr();
    size_t written = Update.writeStream(*stream);
    http.end();

    if (written != (size_t)len) {
        errorOut = "wrote " + String(written) + "/" + String(len) + " bytes - " + Update.errorString();
        Update.abort();
        return false;
    }

    if (!Update.end(true)) {
        errorOut = String("image validation failed: ") + Update.errorString();
        return false;
    }

    ESP.restart();
    return true;  // unreachable - restart() doesn't return
}
