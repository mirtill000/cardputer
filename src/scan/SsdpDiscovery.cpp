#include "SsdpDiscovery.h"
#include <WiFiUdp.h>
#include <cstring>

SsdpDiscovery g_ssdpDiscovery;

namespace {

// Case-insensitive "does this line start with header:" check, then
// returns the trimmed value after the colon. SSDP responses are
// HTTP-status-line-shaped text, not a binary format - ordinary string
// handling is all this needs.
bool extractHeader(const String& line, const char* header, String& out) {
    size_t headerLen = strlen(header);
    if (line.length() <= headerLen) return false;
    String prefix = line.substring(0, headerLen);
    prefix.toUpperCase();
    String upperHeader = header;
    upperHeader.toUpperCase();
    if (prefix != upperHeader) return false;

    String value = line.substring(headerLen);
    value.trim();
    out = value;
    return true;
}

}  // namespace

void SsdpDiscovery::begin(QueueHandle_t outQueue) {
    _mutex = xSemaphoreCreateMutex();
    _outQueue = outQueue;
}

void SsdpDiscovery::start() {
    if (_running) {
        notify("SSDP discovery already running");
        return;
    }
    if (_mutex && xSemaphoreTake(_mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
        _devices.clear();
        xSemaphoreGive(_mutex);
    }
    _running = true;
    notify(ScanEventType::ScanStarted);
    if (xTaskCreatePinnedToCore(&SsdpDiscovery::taskEntry, "ssdp", 4096, this, 1, nullptr, 0) != pdPASS) {
        _running = false;
        notify(ScanEventType::ScanFinished, 100);
    }
}

void SsdpDiscovery::taskEntry(void* arg) {
    static_cast<SsdpDiscovery*>(arg)->run();
    vTaskDelete(nullptr);
}

void SsdpDiscovery::run() {
    WiFiUDP udp;
    if (!udp.begin(0)) {  // bind an ephemeral local port
        notify("failed to open UDP socket");
        _running = false;
        notify(ScanEventType::ScanFinished, 100);
        return;
    }

    // Standard SSDP discovery request (RFC-adjacent, UPnP Device
    // Architecture §1.3.2) - ST: ssdp:all asks every device/service to
    // answer, not just a specific type. MX: 3 tells devices to spread
    // their replies over up to 3 seconds (jitter, so they don't all
    // answer at once) - kListenWindowMs gives a little headroom past that.
    const char* msearch =
        "M-SEARCH * HTTP/1.1\r\n"
        "HOST: 239.255.255.250:1900\r\n"
        "MAN: \"ssdp:discover\"\r\n"
        "MX: 3\r\n"
        "ST: ssdp:all\r\n"
        "\r\n";

    udp.beginPacket(IPAddress(239, 255, 255, 250), 1900);
    udp.write((const uint8_t*)msearch, strlen(msearch));
    udp.endPacket();

    uint32_t start = millis();
    char buf[600];
    while ((millis() - start) < kListenWindowMs) {
        int size = udp.parsePacket();
        if (size > 0) {
            int n = udp.read(buf, sizeof(buf) - 1);
            if (n > 0) {
                buf[n] = '\0';
                Device d;
                d.fromIp = udp.remoteIP();

                String response(buf);
                int lineStart = 0;
                while (lineStart < (int)response.length()) {
                    int lineEnd = response.indexOf('\n', lineStart);
                    if (lineEnd < 0) lineEnd = response.length();
                    String line = response.substring(lineStart, lineEnd);
                    line.trim();  // drops the trailing \r too

                    String value;
                    if (extractHeader(line, "SERVER:", value)) d.server = value;
                    else if (extractHeader(line, "LOCATION:", value)) d.location = value;
                    else if (extractHeader(line, "USN:", value)) d.usn = value;

                    lineStart = lineEnd + 1;
                }

                if (d.server.length() || d.location.length() || d.usn.length()) addDevice(d);
            }
        }
        delay(20);
    }

    udp.stop();
    notify(String((unsigned)deviceCount()) + " device(s) answered");
    _running = false;
    notify(ScanEventType::ScanFinished, 100);
}

void SsdpDiscovery::addDevice(const Device& d) {
    if (_mutex && xSemaphoreTake(_mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
        bool dup = false;
        for (const auto& existing : _devices) {
            if (existing.usn == d.usn && existing.fromIp == d.fromIp) {
                dup = true;
                break;
            }
        }
        if (!dup && _devices.size() < kMaxDevices) _devices.push_back(d);
        xSemaphoreGive(_mutex);
    }
}

void SsdpDiscovery::notify(const String& text) {
    if (!_outQueue) return;
    ScanNotification n;
    n.source = ScanSource::Ssdp;
    n.type = ScanEventType::LogLine;
    n.setText(text.c_str());
    xQueueSend(_outQueue, &n, 0);
}

void SsdpDiscovery::notify(ScanEventType type, uint8_t pct) {
    if (!_outQueue) return;
    ScanNotification n;
    n.source = ScanSource::Ssdp;
    n.type = type;
    n.progressPct = pct;
    xQueueSend(_outQueue, &n, 0);
}

size_t SsdpDiscovery::deviceCount() const {
    if (!_mutex || xSemaphoreTake(_mutex, pdMS_TO_TICKS(100)) != pdTRUE) return 0;
    size_t n = _devices.size();
    xSemaphoreGive(_mutex);
    return n;
}

bool SsdpDiscovery::getDevice(size_t index, Device& out) const {
    if (!_mutex || xSemaphoreTake(_mutex, pdMS_TO_TICKS(100)) != pdTRUE) return false;
    bool ok = index < _devices.size();
    if (ok) out = _devices[_devices.size() - 1 - index];
    xSemaphoreGive(_mutex);
    return ok;
}
