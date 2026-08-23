#pragma once

#include <Arduino.h>
#include <atomic>
#include <vector>
#include "../core/EventQueue.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

// One-shot GATT client walker for the Fase 53 BLE lot. Given the address
// of a BLE device already inventoried by BluetoothManager, it:
//   #2 - connects as CENTRAL, walks services + characteristics + their
//        properties (read/write/notify/indicate + auth requirements),
//        reads the world-readable Device Information Service (0x180A)
//        strings (manufacturer / model / firmware / serial / hardware)
//        when present.
//   #6 - flags weak-pairing / posture: writable characteristics that
//        do NOT require authentication or encryption ("just works"
//        write endpoints), missing LE Secure Connections support hints.
//        Detection only - NO writes are ever performed.
//   #8 - matches every (service UUID, characteristic UUID) pair
//        discovered against BleControlChars (known smart-home actuator
//        control endpoints). If a match is writable, it's a "known
//        vector present" finding. Detection ONLY - it is important that
//        this code path NEVER writes to those characteristics, because
//        writing could physically actuate a lock/plug/bulb.
//   #9 - if the HID service (0x1812) is present, marks the device as
//        an input device (keyboard/mouse) - HID Report Map lives under
//        this service; not read in this lot to keep scope bounded.
//
// Single-in-flight. Only ONE walk runs at a time; the caller (the UI)
// screens gate on isRunning() and re-enter the manager's start(). The
// central role does NOT overlap with the BluetoothManager scanner - the
// walker stops the scan for the duration of the connection and restarts
// it after, so the two never fight for the NimBLE controller state.
//
// OFFENSIVE surface, gated: opening a GATT connection to an unaudited
// third-party device is not the same as reading its advertising - it is
// active behavior that the peer may log, could trigger a pairing prompt,
// and in the worst case could annoy or surprise the owner of that
// device. Same consent flow as CredAudit / ServiceAudit / IoT Creds
// (AppConfig::credAuditEnabled). The UI screen enforces the gate before
// calling start(); this class does not check it itself.
class BleGattClient {
public:
    enum class CharAccess : uint8_t {
        None = 0,
        Read = 1 << 0,
        Write = 1 << 1,
        WriteNoResp = 1 << 2,
        Notify = 1 << 3,
        Indicate = 1 << 4,
    };

    struct Characteristic {
        String uuid;              // "180A" (short) or full 128-bit form
        uint8_t access = 0;        // bitmask of CharAccess
        bool authReq = false;      // one of the descriptors indicates auth-required
        bool encReq = false;       // encryption required
        bool controlHit = false;   // known control vector matched (feature #8)
        String controlLabel;       // human-readable if controlHit
        String value;              // read result, printable only, truncated
    };

    struct Service {
        String uuid;
        std::vector<Characteristic> chars;
    };

    struct WalkResult {
        String targetAddr;
        bool connected = false;
        bool disconnected = false;      // graceful disconnect happened
        uint32_t startMs = 0;
        uint32_t endMs = 0;
        String failureNote;              // non-empty on error

        // Feature #2 - Device Info Service reads (world-readable strings).
        String diManufacturer;
        String diModel;
        String diFirmware;
        String diSerial;
        String diHardware;

        // Feature #6 - posture summary.
        uint16_t writableCharCount = 0;
        uint16_t writableNoAuthCount = 0;
        bool sawHidService = false;      // #9 confirmed by service walk (not just ad)
        bool sawSecureConnHint = false;   // any characteristic reported SC-required

        // Feature #8 - control vector matches (subset of the full char list).
        uint16_t knownControlCount = 0;
        uint16_t knownControlWritable = 0;

        std::vector<Service> services;
    };

    void begin(QueueHandle_t outQueue);

    // Kicks off a walk against `targetAddr`. Returns false if a walk is
    // already in flight or the address is empty. Address must match the
    // BluetoothManager format ("aa:bb:cc:dd:ee:ff").
    bool start(const String& targetAddr);
    void stop();
    bool isRunning() const { return _running; }

    // Live target for the screen header.
    String target() const;
    // Mutex-protected snapshot for the results screen.
    WalkResult result() const;

private:
    static constexpr uint32_t kConnectTimeoutMs = 8000;
    static constexpr uint32_t kOverallTimeoutMs = 30000;
    static constexpr size_t kMaxServices = 20;   // bound RAM footprint per walk
    static constexpr size_t kMaxCharsPerService = 16;
    static constexpr size_t kMaxReadableBytes = 32; // truncation for value dumps

    static void taskEntry(void* arg);
    void run();
    void notify(const String& text);
    void notify(ScanEventType type, uint8_t pct = 0);

    mutable SemaphoreHandle_t _mutex = nullptr;
    QueueHandle_t _outQueue = nullptr;
    std::atomic<bool> _running{false};
    String _target;
    WalkResult _result;
};

extern BleGattClient g_bleGattClient;
