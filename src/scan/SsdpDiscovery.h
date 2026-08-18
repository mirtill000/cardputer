#pragma once

#include <Arduino.h>
#include <atomic>
#include <vector>
#include "../core/EventQueue.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

// Active UPnP/SSDP discovery: sends a standard M-SEARCH request to the
// SSDP multicast group (239.255.255.250:1900) and listens for the
// unicast replies devices send back — smart TVs, media servers, NAS
// boxes, routers, and plenty of IoT gear all answer this on most home/
// office networks, announcing themselves with no probing needed beyond
// the one standard multicast request.
//
// Lower risk than the 802.11-parsing family of features in this
// project (ArpSpoofManager/DeauthManager/PmkidManager/CdpLldpSniffer):
// this is plain UDP over a normal WiFiUDP socket, no promiscuous mode,
// no raw frame parsing — SSDP replies are just HTTP-like text (line-
// delimited "HEADER: value" pairs), no binary format to get wrong.
class SsdpDiscovery {
public:
    struct Device {
        String server;    // SERVER: header - OS/UPnP stack, often reveals the vendor
        String location;  // LOCATION: header - URL to the device's UPnP description XML
        String usn;        // USN: header - unique service name
        IPAddress fromIp;
    };

    void begin(QueueHandle_t outQueue);

    void start();  // no-op if already running
    bool isRunning() const { return _running; }

    size_t deviceCount() const;
    bool getDevice(size_t index, Device& out) const;  // most-recently-seen first

private:
    static constexpr uint32_t kListenWindowMs = 4000;
    static constexpr size_t kMaxDevices = 30;

    static void taskEntry(void* arg);
    void run();
    void addDevice(const Device& d);
    void notify(ScanEventType type, uint8_t pct = 0);
    void notify(const String& text);

    mutable SemaphoreHandle_t _mutex = nullptr;
    std::vector<Device> _devices;
    QueueHandle_t _outQueue = nullptr;
    std::atomic<bool> _running{false};
};

extern SsdpDiscovery g_ssdpDiscovery;
