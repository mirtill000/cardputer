#include "CaptivePortalDetector.h"
#include "WifiManager.h"
#include <WiFiClient.h>

CaptivePortalDetector g_captivePortalDetector;

namespace {
// Standard connectivity-check endpoint (the one Android/Chrome use). Plain
// HTTP on purpose: a captive portal intercepts unencrypted HTTP, which is
// exactly the signal we're looking for. The host resolves through the
// portal's own DNS when one is present — that's fine, we only care whether
// the reply is the expected 204 or an interception.
constexpr char kCheckHost[] = "connectivitycheck.gstatic.com";
constexpr char kCheckPath[] = "/generate_204";
}  // namespace

void CaptivePortalDetector::begin(QueueHandle_t outQueue) {
    _mutex = xSemaphoreCreateMutex();
    _outQueue = outQueue;
}

bool CaptivePortalDetector::start() {
    if (_running) {
        notify("captive check already running");
        return false;
    }
    if (!g_wifi.isConnected()) {
        notify("not connected to WiFi");
        return false;
    }
    if (_mutex && xSemaphoreTake(_mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
        _result = Result{};
        _result.status = Status::Checking;
        xSemaphoreGive(_mutex);
    }
    _running = true;
    if (xTaskCreatePinnedToCore(&CaptivePortalDetector::taskEntry, "captive", 4096, this, 1, nullptr, 0) != pdPASS) {
        _running = false;
        notify("failed to start captive task");
        return false;
    }
    return true;
}

void CaptivePortalDetector::taskEntry(void* arg) {
    static_cast<CaptivePortalDetector*>(arg)->run();
    vTaskDelete(nullptr);
}

void CaptivePortalDetector::run() {
    Result r;
    r.status = Status::NoConnectivity;

    WiFiClient client;
    notify(String("checking ") + kCheckHost + "...");
    if (client.connect(kCheckHost, 80, kTimeoutMs)) {
        client.print(String("GET ") + kCheckPath + " HTTP/1.1\r\n" + "Host: " + kCheckHost + "\r\n" +
                     "Connection: close\r\n\r\n");

        // Read the status line and headers (we only need the first line for
        // the code, plus a Location header if a redirect came back).
        String statusLine;
        String location;
        bool firstLine = true;
        uint32_t start = millis();
        while (client.connected() && (millis() - start) < kTimeoutMs) {
            if (!client.available()) {
                delay(10);
                continue;
            }
            String line = client.readStringUntil('\n');
            line.trim();
            if (firstLine) {
                statusLine = line;
                firstLine = false;
            }
            if (line.length() == 0) break;  // end of headers
            String lower = line;
            lower.toLowerCase();
            if (lower.startsWith("location:")) {
                location = line.substring(9);
                location.trim();
            }
        }
        client.stop();

        // Parse "HTTP/1.x NNN ..." for the numeric status.
        int code = 0;
        int sp = statusLine.indexOf(' ');
        if (sp > 0 && sp + 4 <= (int)statusLine.length()) {
            code = statusLine.substring(sp + 1, sp + 4).toInt();
        }
        r.httpStatus = code;

        if (code == 204) {
            r.status = Status::OpenInternet;  // reached the real endpoint untouched
        } else if (code != 0) {
            // Anything other than the expected 204 on this endpoint means
            // something answered in its place — a captive portal.
            r.status = Status::PortalDetected;
            r.portalUrl = location;
        }
        // code == 0 (unparseable / no reply) stays NoConnectivity.
    }

    switch (r.status) {
        case Status::OpenInternet: notify("no captive portal (204 OK)"); break;
        case Status::PortalDetected:
            notify(r.portalUrl.length() ? (String("portal -> ") + r.portalUrl) : String("captive portal detected"));
            break;
        default: notify("no connectivity to check endpoint"); break;
    }

    if (_mutex && xSemaphoreTake(_mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
        _result = r;
        xSemaphoreGive(_mutex);
    }
    _running = false;
}

void CaptivePortalDetector::notify(const String& text) {
    if (!_outQueue) return;
    ScanNotification n;
    n.source = ScanSource::CaptivePortal;
    n.type = ScanEventType::LogLine;
    n.setText(text.c_str());
    xQueueSend(_outQueue, &n, 0);
}

CaptivePortalDetector::Result CaptivePortalDetector::result() const {
    Result r;
    if (_mutex && xSemaphoreTake(_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        r = _result;
        xSemaphoreGive(_mutex);
    }
    return r;
}
