#include "BluetoothManager.h"
#include "BleCompanyIds.h"
#include "ScanManager.h"
#include "../core/Types.h"
#include <NimBLEDevice.h>
#include <esp_bt.h>   // esp_bt_controller_mem_release: free the Classic-BT
                       // controller RAM we never use (Fase 60 - ~30KB back)
#include <cstdio>
#include <cstring>
#include <new>       // std::bad_alloc caught below in the push_back path
#include <utility>   // std::move for the same push_back

BluetoothManager g_bluetoothManager;

namespace {

// Company IDs we look up structurally (not just the vendor label).
constexpr uint16_t kCompanyApple = 0x004C;
constexpr uint16_t kCompanyMicrosoft = 0x0006;
constexpr uint16_t kCompanySamsung = 0x0075;
constexpr uint16_t kCompanyGoogle = 0x00E0;

// 16-bit service UUIDs we watch for. UUID objects can compare against
// these via NimBLEUUID(uint16_t).
constexpr uint16_t kServiceEddystone = 0xFEAA;
constexpr uint16_t kServiceFastPair = 0xFE2C;
constexpr uint16_t kServiceTile = 0xFEED;
constexpr uint16_t kServiceSamsungTracker = 0xFD5A;
constexpr uint16_t kServiceHid = 0x1812;   // #9 - HID service (used for the vendor tag, not the audit)

String macToStr(const uint8_t b[6]) {
    char out[18];
    snprintf(out, sizeof(out), "%02x:%02x:%02x:%02x:%02x:%02x", b[0], b[1], b[2], b[3], b[4], b[5]);
    return String(out);
}

BluetoothManager::AddrKind classifyAddr(uint8_t addrType, const uint8_t b[6]) {
    // NimBLE addrType: 0 public, 1 random. Random splits by top 2 bits of MSB
    // per BT Core Spec: 0b11 static, 0b01 non-resolvable, 0b00 resolvable (RPA).
    if (addrType == 0) return BluetoothManager::AddrKind::Public;
    uint8_t top = (b[0] >> 6) & 0x3;
    if (top == 0b11) return BluetoothManager::AddrKind::RandomStatic;
    if (top == 0b00) return BluetoothManager::AddrKind::Rpa;
    return BluetoothManager::AddrKind::Random;
}

// Best-effort textual short-services list from an AdvertisedDevice.
String servicesShort(NimBLEAdvertisedDevice* dev, bool& hasHid, bool& hasEddystone, bool& hasFastPair,
                     bool& hasTile, bool& hasSamsungTracker) {
    hasHid = hasEddystone = hasFastPair = hasTile = hasSamsungTracker = false;
    if (!dev || !dev->haveServiceUUID()) return String();
    String out;
    size_t n = dev->getServiceUUIDCount();
    for (size_t i = 0; i < n && i < 8; i++) {
        NimBLEUUID u = dev->getServiceUUID(i);
        // Bit hacky but cheap: NimBLEUUID::toString() returns full 128-bit
        // form even for 16-bit UUIDs; grab the two-byte-relevant nibble
        // slice for display, but do the flag matching by comparing to
        // constructed 16-bit UUIDs.
        if (u.equals(NimBLEUUID(kServiceHid))) hasHid = true;
        if (u.equals(NimBLEUUID(kServiceEddystone))) hasEddystone = true;
        if (u.equals(NimBLEUUID(kServiceFastPair))) hasFastPair = true;
        if (u.equals(NimBLEUUID(kServiceTile))) hasTile = true;
        if (u.equals(NimBLEUUID(kServiceSamsungTracker))) hasSamsungTracker = true;
        std::string s = u.toString();
        // Trim to the short 16-bit form when possible.
        if (s.length() > 8) s = s.substr(4, 4);
        if (out.length()) out += ",";
        out += s.c_str();
    }
    return out;
}

}  // namespace

// --- NimBLE scan callback bridge ---

class BleScanCallbacks : public NimBLEAdvertisedDeviceCallbacks {
public:
    void onResult(NimBLEAdvertisedDevice* dev) override {
        g_bluetoothManager.onAdvertisedDevice(dev);
    }
};

static BleScanCallbacks s_scanCallbacks;

// ---

void BluetoothManager::begin(QueueHandle_t outQueue) {
    _mutex = xSemaphoreCreateMutex();
    _outQueue = outQueue;

    // Fase 60: give NimBLE the heap headroom it needs.
    //
    // Ilaria's second BLE panic was ESP_ERR_NO_MEM inside
    // esp_nimble_hci_init() - NimBLE couldn't find the ~30 KB of
    // contiguous heap it needs for its BT controller state. The standard
    // fix on ESP32 for BLE-only projects is to release the Classic-BT
    // controller's static heap partition, which nothing here ever uses
    // (the Cardputer's radio does BLE + WiFi, never Bluetooth Classic /
    // BR/EDR). Freed once, permanently. Must be called before any
    // NimBLE init - here in begin(), on the main setup task at boot,
    // when the ESP-IDF BT controller has not been brought up yet, is
    // the safe spot.
    esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);

    // Fase 59: pre-reserve the full BleDevice capacity at boot, when
    // the heap is freshest and least fragmented. Without this,
    // push_back grows the vector geometrically (2, 4, 8, ..., 64) and
    // EACH resize allocates a new contiguous block and copies every
    // existing BleDevice into it - each copy allocates ~10 Strings for
    // the fields. Under BLE load ilaria's hardware hit std::bad_alloc
    // during that grow-copy path. Reserving up front means every
    // push_back below is O(1) and only allocates the Strings of the
    // new device, not a copy of the entire history.
    _devices.reserve(kMaxDevices);
    // Permanent-idle-task pattern (same shape as CdpLldpSniffer /
    // PassiveHostDiscovery): the task lives forever, start()/stop()
    // toggle the internal running flag.
    //
    // Stack: 8192 (was 4096 in Fase 54, crashed on ilaria's hardware).
    // The task itself doesn't need it, but the NimBLE scan callback -
    // BleScanCallbacks::onResult -> onAdvertisedDevice - runs on the
    // NimBLE host task, and the parsing chain (Apple Continuity, iBeacon,
    // beacons, trackers, service data walk) allocates a lot of Strings.
    // The scan callback stack IS this task's stack in some NimBLE
    // configurations; oversizing here is a cheap way to give the callback
    // slack without touching NimBLE config.
    xTaskCreatePinnedToCore(&BluetoothManager::taskEntry, "blescan", 8192, this, 1, nullptr, 0);
}

void BluetoothManager::start() { _running = true; }
void BluetoothManager::stop() { _running = false; }

void BluetoothManager::taskEntry(void* arg) {
    static_cast<BluetoothManager*>(arg)->run();
}

void BluetoothManager::run() {
    // Fase 58: NimBLE is init'd ONCE (lazily on the first start()) and
    // never deinit'd. Cycling init/deinit(true) on NimBLE-Arduino 1.4.1 +
    // arduino-esp32 2.0.17 is a well-documented panic path (see
    // h2zero/NimBLE-Arduino issues) - the internal host task state gets
    // trashed on re-init and the second start() panics inside the BT
    // controller. The RAM cost of leaving NimBLE up while idle (~30 KB
    // of BSS - what Fase 14 was worried about) is a fair trade for a
    // BLE SCAN screen that actually opens twice in a row without a
    // reboot.
    for (;;) {
        if (_running) {
            if (!_initialized) {
                _beaconCount = 0;
                _trackerCount = 0;
                _correlatedCount = 0;
                _rpaMatchedCount = 0;
                _hidCount = 0;
                NimBLEDevice::init("");
                // Very low TX - we never advertise, keeping the radio quiet
                // reduces BLE/WiFi coex contention when both are up.
                NimBLEDevice::setPower(ESP_PWR_LVL_N12);
                NimBLEScan* scan = NimBLEDevice::getScan();
                scan->setAdvertisedDeviceCallbacks(&s_scanCallbacks, /*wantDuplicates=*/true);
                scan->setActiveScan(false);   // passive scan - no SCAN_REQ
                scan->setInterval(160);        // 100ms in 0.625ms slots
                scan->setWindow(160);
                _initialized = true;
                notify("BLE stack up");
            }
            NimBLEScan* scan = NimBLEDevice::getScan();
            if (!scan->isScanning()) {
                scan->start(0, nullptr, /*is_continue=*/false);  // continuous
                notify("BLE scanner on");
            }
        } else {
            // On stop: pause the scanner but leave the NimBLE stack up.
            // Re-entering BLE SCAN just calls scan->start() again on the
            // same live stack - no re-init, no panic.
            if (_initialized) {
                NimBLEScan* scan = NimBLEDevice::getScan();
                if (scan->isScanning()) {
                    scan->stop();
                    notify("BLE scanner off");
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

void BluetoothManager::onAdvertisedDevice(const void* nimbleDev) {
    if (!nimbleDev) return;
    auto* dev = static_cast<NimBLEAdvertisedDevice*>(const_cast<void*>(nimbleDev));

    // Build a fresh snapshot first; mutex-protected merge happens at the end.
    BleDevice bd;
    NimBLEAddress addr = dev->getAddress();
    const uint8_t* rawAddr = addr.getNative();
    // NimBLE stores addresses little-endian internally; format big-endian for display.
    uint8_t be[6] = {rawAddr[5], rawAddr[4], rawAddr[3], rawAddr[2], rawAddr[1], rawAddr[0]};
    bd.addr = macToStr(be);
    bd.addrKind = classifyAddr(addr.getType(), be);
    bd.rssi = (int8_t)dev->getRSSI();
    if (dev->haveTXPower()) bd.txPower = (int8_t)dev->getTXPower();
    if (dev->haveName()) bd.name = dev->getName().c_str();
    if (dev->haveAppearance()) bd.appearance = dev->getAppearance();

    bool hasHid = false, hasEddystone = false, hasFastPair = false, hasTile = false, hasSamsungTracker = false;
    bd.services = servicesShort(dev, hasHid, hasEddystone, hasFastPair, hasTile, hasSamsungTracker);

    // --- Manufacturer data parsing ---
    if (dev->haveManufacturerData()) {
        std::string md = dev->getManufacturerData();
        if (md.size() >= 2) {
            const uint8_t* mdb = reinterpret_cast<const uint8_t*>(md.data());
            bd.companyId = (uint16_t)mdb[0] | ((uint16_t)mdb[1] << 8);
            const char* vendorName = ble_company_ids::lookup(bd.companyId);
            if (vendorName) bd.vendor = vendorName;

            if (bd.companyId == kCompanyApple) {
                parseAppleContinuity(mdb, md.size(), bd);
                parseIBeacon(mdb, md.size(), bd);
                parseTracker(mdb, md.size(), bd.companyId, bd);
            } else if (bd.companyId == kCompanyMicrosoft) {
                parseSwiftPair(mdb, md.size(), bd);
            } else {
                // AltBeacon carries its "0xBEAC" tag AFTER the companyId, so any
                // vendor could publish one - try regardless of who the vendor is.
                parseAltBeacon(mdb, md.size(), bd);
            }
        }
    }

    // --- Service data parsing (feature #5 non-Apple beacons, #4 Fast Pair, #7 non-Apple trackers) ---
    if (dev->haveServiceData()) {
        size_t sdCount = dev->getServiceDataCount();
        for (size_t i = 0; i < sdCount; i++) {
            NimBLEUUID svcUuid = dev->getServiceDataUUID(i);
            std::string sd = dev->getServiceData(i);
            const uint8_t* sdb = reinterpret_cast<const uint8_t*>(sd.data());
            if (svcUuid.equals(NimBLEUUID(kServiceEddystone))) {
                parseEddystone(sdb, sd.size(), bd);
            } else if (svcUuid.equals(NimBLEUUID(kServiceFastPair))) {
                parseFastPair(sdb, sd.size(), bd);
            } else if (svcUuid.equals(NimBLEUUID(kServiceTile))) {
                bd.tracker = TrackerKind::Tile;
                bd.trackerNote = "Tile";
                if (bd.vendor.isEmpty()) bd.vendor = "Tile";
            } else if (svcUuid.equals(NimBLEUUID(kServiceSamsungTracker))) {
                bd.tracker = TrackerKind::SamsungSmartTag;
                bd.trackerNote = "Samsung SmartTag (SmartThings Find)";
                if (bd.vendor.isEmpty()) bd.vendor = "Samsung";
            }
        }
    }

    // HID advertised as service UUID. Fase 54 flagged this in
    // platformNote only; Fase 55 also promotes it to a first-class
    // BleDevice::hidService flag so the HID dashboard can filter on
    // it without re-scanning the platformNote string.
    if (hasHid) {
        bd.hidService = true;
        if (bd.platformNote.isEmpty()) bd.platformNote = "HID (input device)";
    }

    // Feature #10 WiFi correlation used to run here in the NimBLE callback
    // context. Fase 58 moved it out - grabbing g_scanManager's mutex from
    // the BT host task caused panics on real hardware (short callback
    // stack + external mutex take is a classic ESP32 crash pattern).
    // Now it's lazy: get()/getFirstXxx() enrich each row on read, in the
    // UI task's context where mutex taking is safe.

    // --- Merge into device table ---
    bool newDevice = false;
    bool newBeacon = false, newTracker = false, newCorrelated = false;
    bool newRpaMatch = false, newHid = false;
    if (_mutex && xSemaphoreTake(_mutex, pdMS_TO_TICKS(80)) == pdTRUE) {
        BleDevice* existing = nullptr;
        for (auto& d : _devices) {
            if (d.addr == bd.addr) {
                existing = &d;
                break;
            }
        }
        if (existing) {
            if (bd.hidService && !existing->hidService) {
                existing->hidService = true;
                newHid = true;
            }
            existing->rssi = bd.rssi;
            existing->lastSeenMs = millis();
            existing->sightings++;
            // Merge additive fields - later ads may fill in what earlier ones didn't.
            if (bd.name.length() && existing->name.isEmpty()) existing->name = bd.name;
            if (bd.vendor.length() && existing->vendor.isEmpty()) existing->vendor = bd.vendor;
            if (bd.services.length() && existing->services.isEmpty()) existing->services = bd.services;
            if (bd.platformNote.length() && existing->platformNote.isEmpty())
                existing->platformNote = bd.platformNote;
            if (bd.beacon != BeaconKind::None && existing->beacon == BeaconKind::None) {
                existing->beacon = bd.beacon;
                existing->beaconNote = bd.beaconNote;
                newBeacon = true;
            }
            if (bd.tracker != TrackerKind::None && existing->tracker == TrackerKind::None) {
                existing->tracker = bd.tracker;
                existing->trackerNote = bd.trackerNote;
                newTracker = true;
            }
            if (bd.correlatedWifiIp.length() && existing->correlatedWifiIp.isEmpty()) {
                existing->correlatedWifiIp = bd.correlatedWifiIp;
                newCorrelated = true;
            }
        } else if (_devices.size() < kMaxDevices) {
            bd.firstSeenMs = millis();
            bd.lastSeenMs = bd.firstSeenMs;
            bd.sightings = 1;
            // #3 (Fase 55): for RPA addresses, look for an existing device
            // with the same stable fingerprint (companyId + services +
            // appearance + platformNote) - if we find one, this is likely
            // the same physical device that has rotated its private
            // address (RPAs rotate ~every 15 minutes per BT Core Spec).
            if (bd.addrKind == AddrKind::Rpa) {
                String match = findRpaMatchLocked(bd);
                if (match.length()) {
                    bd.sameAsAddr = match;
                    newRpaMatch = true;
                }
            }
            newBeacon = (bd.beacon != BeaconKind::None);
            newTracker = (bd.tracker != TrackerKind::None);
            newCorrelated = bd.correlatedWifiIp.length() > 0;
            newHid = bd.hidService;
            // Fase 59: reserved capacity in begin() means push_back doesn't
            // reallocate the vector. It still allocates String storage for
            // the new BleDevice, though - if the heap is exhausted (BT +
            // WiFi both busy) that can throw bad_alloc. Wrapping in try/
            // catch drops the ad silently instead of aborting the whole
            // firmware (backtrace: bad_alloc -> terminate -> abort was
            // ilaria's crash before the reserve() up top). std::move
            // avoids one round of String copies vs the old push_back(bd).
            try {
                _devices.push_back(std::move(bd));
                newDevice = true;
            } catch (const std::bad_alloc&) {
                // Silently drop this advertisement - the device will
                // reappear on the next ad it broadcasts, when the heap
                // may be less pressured.
            }
        }
        xSemaphoreGive(_mutex);
    }

    if (newBeacon) _beaconCount++;
    if (newTracker) _trackerCount++;
    if (newCorrelated) _correlatedCount++;
    if (newRpaMatch) _rpaMatchedCount++;
    if (newHid) _hidCount++;

    if (newDevice) {
        String log = "BLE: " + bd.addr;
        if (bd.name.length()) log += " (" + bd.name + ")";
        if (log.length() > 38) log = log.substring(0, 38);
        notify(log);
    }
}

// --- Parsers -------------------------------------------------------------

void BluetoothManager::parseAppleContinuity(const uint8_t* data, size_t len, BleDevice& out) {
    // Layout: [4C 00] [type(1)] [subLen(1)] [payload(subLen)] [type(1)] ...
    // See: https://adamcatley.com/AirTag.html and community reversing.
    if (len < 4) return;
    size_t pos = 2;
    while (pos + 1 < len) {
        uint8_t type = data[pos++];
        uint8_t subLen = data[pos++];
        if (pos + subLen > len) break;
        switch (type) {
            case 0x02:  // iBeacon - parsed separately (parseIBeacon walks the same layout)
                break;
            case 0x05: out.platformNote = "Apple AirDrop"; break;
            case 0x07: out.platformNote = "Apple Nearby Info"; break;
            case 0x09: out.platformNote = "Apple Handoff"; break;
            case 0x0A: out.platformNote = "Apple Wi-Fi Settings"; break;
            case 0x0B: out.platformNote = "Apple Watch link"; break;
            case 0x0C: out.platformNote = "Apple Handoff (HomeKit)"; break;
            case 0x0D: out.platformNote = "Apple HomeKit"; break;
            case 0x0F: out.platformNote = "Apple Nearby Action"; break;
            case 0x10: out.platformNote = "Apple Nearby Info (state)"; break;
            case 0x12:  // FindMy / offline finding — see parseTracker
                break;
            default: {
                if (out.platformNote.isEmpty()) {
                    char b[24];
                    snprintf(b, sizeof(b), "Apple ctnty 0x%02x", type);
                    out.platformNote = b;
                }
                break;
            }
        }
        pos += subLen;
    }
    if (out.vendor.isEmpty()) out.vendor = "Apple";
}

void BluetoothManager::parseSwiftPair(const uint8_t* data, size_t len, BleDevice& out) {
    // Microsoft Swift Pair manufacturer data: [06 00] [03] [MS-BLE-BEACON marker]
    // Anything starting with 06 00 03 is a very good hint. Full spec:
    // https://learn.microsoft.com/en-us/windows-hardware/design/component-guidelines/bluetooth-swift-pair
    if (len < 3) return;
    if (data[2] == 0x03) {
        out.platformNote = "Microsoft Swift Pair";
        if (out.vendor.isEmpty()) out.vendor = "Microsoft";
    }
}

void BluetoothManager::parseIBeacon(const uint8_t* data, size_t len, BleDevice& out) {
    // Apple iBeacon: [4C 00 02 15] [16-byte UUID] [2B major] [2B minor] [1B tx]
    if (len < 25) return;
    if (data[2] != 0x02 || data[3] != 0x15) return;
    char buf[64];
    // Print first 4 bytes of the UUID; a full 16-byte UUID hex string would
    // wildly overflow the 40-byte ScanNotification text.
    uint16_t major = ((uint16_t)data[20] << 8) | data[21];
    uint16_t minor = ((uint16_t)data[22] << 8) | data[23];
    snprintf(buf, sizeof(buf), "iBeacon %02x%02x%02x%02x M:%u m:%u", data[4], data[5], data[6], data[7],
             (unsigned)major, (unsigned)minor);
    out.beacon = BeaconKind::IBeacon;
    out.beaconNote = buf;
}

void BluetoothManager::parseAltBeacon(const uint8_t* data, size_t len, BleDevice& out) {
    // AltBeacon: any companyId + [BE AC] + [16-byte ID] + [2B major] + [2B minor] + [1B ref RSSI] + [1B reserved]
    if (len < 25) return;
    if (data[2] != 0xBE || data[3] != 0xAC) return;
    char buf[64];
    uint16_t major = ((uint16_t)data[20] << 8) | data[21];
    uint16_t minor = ((uint16_t)data[22] << 8) | data[23];
    snprintf(buf, sizeof(buf), "AltBeacon %02x%02x M:%u m:%u", data[4], data[5], (unsigned)major, (unsigned)minor);
    out.beacon = BeaconKind::AltBeacon;
    out.beaconNote = buf;
}

void BluetoothManager::parseEddystone(const uint8_t* data, size_t len, BleDevice& out) {
    // First byte = frame type: 0x00 UID, 0x10 URL, 0x20 TLM, 0x30 EID
    if (len < 1) return;
    char buf[48];
    switch (data[0]) {
        case 0x00:
            snprintf(buf, sizeof(buf), "Eddystone-UID");
            break;
        case 0x10:
            snprintf(buf, sizeof(buf), "Eddystone-URL");
            break;
        case 0x20:
            snprintf(buf, sizeof(buf), "Eddystone-TLM");
            break;
        case 0x30:
            snprintf(buf, sizeof(buf), "Eddystone-EID");
            break;
        default:
            snprintf(buf, sizeof(buf), "Eddystone fr:0x%02x", data[0]);
            break;
    }
    out.beacon = BeaconKind::Eddystone;
    out.beaconNote = buf;
    if (out.vendor.isEmpty()) out.vendor = "Google (Eddystone)";
}

void BluetoothManager::parseFastPair(const uint8_t* data, size_t len, BleDevice& out) {
    // Google Fast Pair: 3-byte model ID (paired-nearby state) or 6-byte
    // model ID + account-key filter (unpaired discovery). Model ID
    // resolution requires an online lookup at Google's Fast Pair
    // registry, so we don't attempt to name the model - just flag Fast
    // Pair presence and, when present, print the model-ID nibble prefix.
    if (out.vendor.isEmpty()) out.vendor = "Google (Fast Pair)";
    if (len >= 3) {
        char buf[32];
        snprintf(buf, sizeof(buf), "Fast Pair id:%02x%02x%02x", data[0], data[1], data[2]);
        out.platformNote = buf;
    } else {
        out.platformNote = "Fast Pair";
    }
}

void BluetoothManager::parseTracker(const uint8_t* data, size_t len, uint16_t companyId, BleDevice& out) {
    // Apple FindMy / offline-finding: manufacturer 0x004C, type 0x12.
    // Subtype in the byte after subLen tells "nearby" vs "offline / lost".
    // Community writeups: 0x19 (nearby), 0x00 (offline finding unpaired).
    if (companyId != kCompanyApple || len < 5) return;
    size_t pos = 2;
    while (pos + 1 < len) {
        uint8_t type = data[pos++];
        uint8_t subLen = data[pos++];
        if (pos + subLen > len) break;
        if (type == 0x12 && subLen >= 1) {
            out.tracker = TrackerKind::AppleFindMy;
            uint8_t status = data[pos];
            out.trackerNote = (status == 0x19) ? "Apple FindMy (nearby)"
                              : (status == 0x00) ? "Apple FindMy (offline)"
                                                 : "Apple FindMy";
            if (out.vendor.isEmpty()) out.vendor = "Apple";
            break;
        }
        pos += subLen;
    }
}

void BluetoothManager::correlateWithWifi(BleDevice& out) {
    // Feature #10: match BLE vendor against WiFi-side vendor for any alive
    // host — a best-effort join, not a strong claim (BLE and WiFi are
    // separate interfaces on a phone, so the MAC differs; matching by
    // vendor is a cheap first pass that pays off for single-vendor
    // devices like AirPods/AppleTV/etc.).
    if (out.vendor.isEmpty()) return;
    String v = out.vendor;
    v.toLowerCase();
    size_t hosts = g_scanManager.hostCount();
    HostInfo h;
    for (size_t i = 0; i < hosts; i++) {
        if (!g_scanManager.getHost(i, h) || !h.alive) continue;
        String hv = h.vendor;
        hv.toLowerCase();
        if (hv.length() && (v.indexOf(hv) >= 0 || hv.indexOf(v) >= 0)) {
            out.correlatedWifiIp = h.ip.toString();
            return;
        }
    }
}

// --- Public accessors ---

uint32_t BluetoothManager::deviceCount() const {
    if (!_mutex || xSemaphoreTake(_mutex, pdMS_TO_TICKS(80)) != pdTRUE) return 0;
    uint32_t n = (uint32_t)_devices.size();
    xSemaphoreGive(_mutex);
    return n;
}

bool BluetoothManager::get(size_t index, BleDevice& out) const {
    if (!_mutex || xSemaphoreTake(_mutex, pdMS_TO_TICKS(80)) != pdTRUE) return false;
    bool ok = index < _devices.size();
    if (ok) out = _devices[_devices.size() - 1 - index];  // most-recently-seen first
    xSemaphoreGive(_mutex);
    // Fase 58: WiFi correlation (feature #10) moved here from the NimBLE
    // scan callback. Done AFTER releasing our own mutex - correlateWithWifi
    // takes ScanManager's mutex, so nesting is out of order. The trade-off
    // is we re-do the vendor match every time the UI reads a row (~30/s in
    // the worst case), but it's a linear scan over ~5-50 hosts, cheap.
    if (ok && out.correlatedWifiIp.isEmpty()) const_cast<BluetoothManager*>(this)->correlateWithWifi(out);
    return ok;
}

bool BluetoothManager::getFirstTracker(size_t index, BleDevice& out) const {
    if (!_mutex || xSemaphoreTake(_mutex, pdMS_TO_TICKS(80)) != pdTRUE) return false;
    size_t seen = 0;
    bool ok = false;
    for (auto it = _devices.rbegin(); it != _devices.rend(); ++it) {
        if (it->tracker == TrackerKind::None) continue;
        if (seen == index) {
            out = *it;
            ok = true;
            break;
        }
        seen++;
    }
    xSemaphoreGive(_mutex);
    if (ok && out.correlatedWifiIp.isEmpty()) const_cast<BluetoothManager*>(this)->correlateWithWifi(out);
    return ok;
}

void BluetoothManager::notify(const String& text) {
    if (!_outQueue) return;
    ScanNotification n;
    n.source = ScanSource::Bluetooth;
    n.type = ScanEventType::LogLine;
    n.setText(text.c_str());
    xQueueSend(_outQueue, &n, 0);
}

void BluetoothManager::notify(ScanEventType type, uint8_t pct) {
    if (!_outQueue) return;
    ScanNotification n;
    n.source = ScanSource::Bluetooth;
    n.type = type;
    n.progressPct = pct;
    xQueueSend(_outQueue, &n, 0);
}

// --- Fase 55: RPA correlation + filtered views + address lookup ---

String BluetoothManager::fingerprint(const BleDevice& d) {
    // The stable half of a BLE advertiser's identity: what a hardware
    // vendor's firmware puts in ads regardless of which RPA it's cycling
    // right now. Empty when there's nothing distinguishing at all - a
    // bare RPA with no manufacturer/service/appearance data can't be
    // correlated back to anything specific and shouldn't be silently
    // merged with unrelated devices.
    String fp;
    if (d.companyId) {
        char b[8];
        snprintf(b, sizeof(b), "co:%04X", (unsigned)d.companyId);
        fp += b;
    }
    if (d.appearance) {
        char b[10];
        snprintf(b, sizeof(b), "|ap:%04X", (unsigned)d.appearance);
        fp += b;
    }
    if (d.services.length()) fp += "|sv:" + d.services;
    if (d.platformNote.length()) fp += "|pn:" + d.platformNote;
    return fp;
}

String BluetoothManager::findRpaMatchLocked(const BleDevice& fresh) const {
    String freshFp = fingerprint(fresh);
    if (freshFp.length() < 4) return String();  // not enough to be distinctive
    // Prefer the earliest observed match so a chain of RPAs collapses to
    // one "root" address - lets the UI show "= <first-seen>" consistently
    // across a session.
    const BleDevice* best = nullptr;
    for (const auto& d : _devices) {
        if (d.addr == fresh.addr) continue;
        if (d.addrKind != AddrKind::Rpa) continue;
        if (fingerprint(d) != freshFp) continue;
        if (!best || d.firstSeenMs < best->firstSeenMs) best = &d;
    }
    return best ? best->addr : String();
}

bool BluetoothManager::getFirstHid(size_t index, BleDevice& out) const {
    if (!_mutex || xSemaphoreTake(_mutex, pdMS_TO_TICKS(80)) != pdTRUE) return false;
    size_t seen = 0;
    bool ok = false;
    for (auto it = _devices.rbegin(); it != _devices.rend(); ++it) {
        if (!it->hidService) continue;
        if (seen == index) { out = *it; ok = true; break; }
        seen++;
    }
    xSemaphoreGive(_mutex);
    if (ok && out.correlatedWifiIp.isEmpty()) const_cast<BluetoothManager*>(this)->correlateWithWifi(out);
    return ok;
}

bool BluetoothManager::findByAddr(const String& addr, BleDevice& out) const {
    if (!_mutex || xSemaphoreTake(_mutex, pdMS_TO_TICKS(80)) != pdTRUE) return false;
    bool ok = false;
    for (const auto& d : _devices) {
        if (d.addr == addr) { out = d; ok = true; break; }
    }
    xSemaphoreGive(_mutex);
    if (ok && out.correlatedWifiIp.isEmpty()) const_cast<BluetoothManager*>(this)->correlateWithWifi(out);
    return ok;
}
