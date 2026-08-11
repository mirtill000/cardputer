#pragma once

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
// RISK — the single least-verified piece of code in this whole
// project: this is the first time anything in this codebase has used
// the classic Arduino-ESP32 BLE library (BLEDevice.h/BLEScan.h/
// BLEAdvertisedDevice.h, bundled with the arduino-esp32 core - no extra
// lib_deps needed), and that library's exact API shape has changed
// across arduino-esp32 core versions (BLEScanResults being returned by
// value vs. by pointer, in particular). If this fails to compile, the
// call to BLEDevice::getScan()->start(...) in BleScanManager.cpp is the
// first place to check against whatever arduino-esp32 core version
// this project is actually building against.
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
