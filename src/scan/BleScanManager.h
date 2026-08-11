#pragma once

#include <Arduino.h>
#include <atomic>
#include <vector>
#include "../core/EventQueue.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

// Continuous background BLE survey - the Bluetooth sibling of
// WardrivingManager's WiFi one, same passive-only philosophy: scans for
// nearby BLE advertisements and logs every device seen (address, name
// if advertised, RSSI) to the SD card. Never connects/pairs/writes to
// anything - there's no active behavior here at all, unlike
// WardrivingManager's allowlist-gated WiFi join, because pairing with a
// BLE device is a materially more invasive action than joining an open
// WiFi network and this assistant isn't automating that.
//
// RISK (confirmed on real hardware, see git log): this is the first
// time anything in this codebase has used the classic Arduino-ESP32 BLE
// library (BLEDevice.h/BLEScan.h/BLEAdvertisedDevice.h, bundled with
// the arduino-esp32 core - no extra lib_deps needed). Its API shape has
// changed across core versions, and two real build failures have come
// out of this file already:
//   - BLEScan::start() returns BLEScanResults BY VALUE on the core
//     version this project actually builds against (LDF reports
//     "ESP32 BLE Arduino @ 2.0.0"), not by pointer as some other core
//     releases have it. BleScanManager.cpp uses the by-value form.
//   - BLEAddress::toString() and BLEAdvertisedDevice::getName() return
//     std::string on this build, not Arduino String. BleScanManager.cpp
//     wraps both in String(x.c_str()), which works either way (Arduino
//     String also has c_str()) - don't drop that wrapping even if a
//     future core makes it look redundant.
// If a future core bump breaks the build again, BleScanManager.cpp is
// the line to check first.
class BleScanManager {
public:
    struct BleSighting {
        String address;  // "aa:bb:cc:dd:ee:ff"
        String name;      // "" if never advertised
        int32_t rssi = 0;
        uint32_t firstSeenMs = 0;
        uint32_t lastSeenMs = 0;
    };

    void begin(QueueHandle_t outQueue);

    void start();  // no-op if already running
    void stop();
    bool isRunning() const { return _running; }

    // Most-recently-seen first.
    size_t sightingCount() const;
    bool getSighting(size_t index, BleSighting& out) const;

private:
    static constexpr size_t kMaxSightings = 200;
    static constexpr uint32_t kScanIntervalMs = 6000;  // on top of the scan's own ~4s active duration
    static constexpr uint8_t kBleScanSeconds = 4;

    static void taskEntry(void* arg);
    void run();
    void runScanCycle();
    void logSighting(const BleSighting& d);
    void notify(const String& text);

    mutable SemaphoreHandle_t _mutex = nullptr;
    std::vector<BleSighting> _sightings;
    QueueHandle_t _outQueue = nullptr;

    std::atomic<bool> _running{false};
    bool _bleInitialized = false;  // BLEDevice::init() is lazy - only paid for if this feature is ever started
};

extern BleScanManager g_bleScanManager;
