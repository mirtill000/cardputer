#pragma once

#include <Arduino.h>
#include <atomic>
#include <vector>
#include "../core/EventQueue.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

// BLE inventory + fingerprinting (Fase 52, first BLE lot after Fase 14
// deliberately removed BLE for flash pressure — see README). NimBLE
// observer-only: this device is a SCANNER, it never advertises, connects,
// pairs, or writes anywhere. Every advertising packet arrives through
// the same NimBLE scan callback and gets classified once per device:
//
//   #1 Inventory              -> addr/RSSI/name/vendor/services (BleDevice)
//   #4 Continuity/Fast Pair /
//      Swift Pair             -> BleDevice::platformNote
//   #5 iBeacon/Eddystone/
//      AltBeacon              -> BleDevice::beacon (kind + short summary)
//   #7 Unwanted-tracker       -> BleDevice::tracker (kind + note)
//   #10 WiFi correlation      -> BleDevice::correlatedWifiIp (looked up
//                                against ScanManager's alive host table by
//                                vendor match — best-effort; MACs across
//                                Classic-BT/BLE and WiFi are on separate
//                                interfaces so a direct MAC join isn't
//                                possible without extra assumptions)
//
// Feature GAP/CENTRAL work (GATT enum, weak-pairing audit, HID detect,
// control-characteristic detect - proposals #2/#6/#8/#9) is DELIBERATELY
// out of this first BLE lot to keep the flash cost of reintroducing BLE
// bounded: NimBLE is configured OBSERVER-only via build_flags in
// platformio.ini (CONFIG_BT_NIMBLE_ROLE_CENTRAL_DISABLED &co), so the
// GATT client code paths are not even linked in. Extending later means
// (a) turning those flags back on, and (b) careful re-measurement of the
// binary size against the OTA slot cap — see Fase 14/README.
//
// Shared radio: BLE and WiFi coexist on the ESP32-S3's 2.4 GHz radio via
// the SoC's built-in coex layer; running this alongside the WiFi tools
// (war driving, promiscuous sniffers) is safe but throughput on both
// degrades. The RF indicator (ActivityStatus) gets a BLE tag when this
// scanner is running, mirroring how the WiFi promiscuous consumers are
// surfaced there.
class BluetoothManager {
public:
    enum class AddrKind : uint8_t { Public, Random, RandomStatic, Rpa, Unknown };
    enum class BeaconKind : uint8_t { None, IBeacon, Eddystone, AltBeacon };
    enum class TrackerKind : uint8_t { None, AppleFindMy, Tile, SamsungSmartTag };

    struct BleDevice {
        String addr;              // "aa:bb:cc:dd:ee:ff"
        AddrKind addrKind = AddrKind::Unknown;
        int8_t rssi = 0;
        int8_t txPower = 0;        // 0 if not advertised
        String name;               // may be empty
        String vendor;             // resolved from company ID or Apple/Samsung/Google fingerprint
        uint16_t companyId = 0;    // 0 if no manufacturer data
        uint16_t appearance = 0;   // 0 if not advertised (Bluetooth SIG "appearance")
        String services;           // short list, e.g. "180F, FE2C"
        String platformNote;       // #4 - "Apple Handoff", "Swift Pair", "Fast Pair", etc.
        BeaconKind beacon = BeaconKind::None;
        String beaconNote;         // e.g. "iBeacon <uuid-prefix> M:12/34"
        TrackerKind tracker = TrackerKind::None;
        String trackerNote;
        String correlatedWifiIp;   // #10 - non-empty if a WiFi host with matching vendor is on-net
        uint32_t firstSeenMs = 0;
        uint32_t lastSeenMs = 0;
        uint32_t sightings = 0;    // how many advertisements we've received from this addr
    };

    void begin(QueueHandle_t outQueue);

    void start();  // no-op if already running
    void stop();
    bool isRunning() const { return _running; }

    // Live counters for the header/dashboard.
    uint32_t deviceCount() const;
    uint32_t beaconCount() const { return _beaconCount; }
    uint32_t trackerCount() const { return _trackerCount; }
    uint32_t correlatedCount() const { return _correlatedCount; }

    // Most-recently-seen first (like the WiFi war-driving list).
    bool get(size_t index, BleDevice& out) const;
    // Filtered views used by BleTrackerScreen / dedicated dashboards.
    bool getFirstTracker(size_t index, BleDevice& out) const;

private:
    static constexpr size_t kMaxDevices = 60;

    static void taskEntry(void* arg);
    void run();

    // Called from the NimBLE callback context - keeps parsing and mutex
    // work self-contained here so the callback can stay a thin adapter.
    void onAdvertisedDevice(const void* nimbleDev);

    // Parsing helpers (kept in the .cpp so nimble types don't leak into
    // this header).
    void parseAppleContinuity(const uint8_t* data, size_t len, BleDevice& out);
    void parseSwiftPair(const uint8_t* data, size_t len, BleDevice& out);
    void parseIBeacon(const uint8_t* data, size_t len, BleDevice& out);
    void parseAltBeacon(const uint8_t* data, size_t len, BleDevice& out);
    void parseEddystone(const uint8_t* data, size_t len, BleDevice& out);
    void parseFastPair(const uint8_t* data, size_t len, BleDevice& out);
    void parseTracker(const uint8_t* data, size_t len, uint16_t companyId, BleDevice& out);
    void correlateWithWifi(BleDevice& out);

    void notify(const String& text);
    void notify(ScanEventType type, uint8_t pct = 0);

    mutable SemaphoreHandle_t _mutex = nullptr;
    std::vector<BleDevice> _devices;
    QueueHandle_t _outQueue = nullptr;

    std::atomic<bool> _running{false};
    std::atomic<bool> _initialized{false};  // NimBLE stack up
    std::atomic<uint32_t> _beaconCount{0};
    std::atomic<uint32_t> _trackerCount{0};
    std::atomic<uint32_t> _correlatedCount{0};
};

extern BluetoothManager g_bluetoothManager;
