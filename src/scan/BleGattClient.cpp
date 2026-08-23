#include "BleGattClient.h"
#include "BleControlChars.h"
#include "BluetoothManager.h"
#include <NimBLEDevice.h>
#include <cstring>

BleGattClient g_bleGattClient;

namespace {

constexpr uint16_t kHidService = 0x1812;
constexpr uint16_t kDeviceInfoService = 0x180A;
constexpr uint16_t kDiManufacturer = 0x2A29;
constexpr uint16_t kDiModel = 0x2A24;
constexpr uint16_t kDiFirmware = 0x2A26;
constexpr uint16_t kDiSerial = 0x2A25;
constexpr uint16_t kDiHardware = 0x2A27;

// Parse an "aa:bb:cc:dd:ee:ff" string into a NimBLEAddress (public type 0
// as the default; NimBLE lets you connect to a random address too, but
// the address kind isn't preserved by BluetoothManager once observed,
// and NimBLE will fall back to scanning the connectable set when a raw
// address without matching type is provided).
NimBLEAddress parseAddr(const String& s) {
    if (s.length() != 17) return NimBLEAddress();
    uint8_t out[6] = {0};
    for (int i = 0; i < 6; i++) {
        String h = s.substring(i * 3, i * 3 + 2);
        out[i] = (uint8_t)strtoul(h.c_str(), nullptr, 16);
    }
    // NimBLE addresses are little-endian in-memory; input is big-endian display.
    uint8_t le[6] = {out[5], out[4], out[3], out[2], out[1], out[0]};
    return NimBLEAddress(le, 0);  // 0 = public. For random we lose auth-required semantics anyway.
}

// Truncate + printable-only sanitize (chars < 0x20 or > 0x7E -> '.'),
// same convention MITM harvest uses (Fase 50). Bounded to keep the
// screen row and the ScanNotification text field readable.
String printable(const uint8_t* data, size_t len, size_t maxLen) {
    if (!data || !len) return String();
    size_t n = (len > maxLen) ? maxLen : len;
    String out;
    out.reserve(n);
    for (size_t i = 0; i < n; i++) {
        char c = (char)data[i];
        if (c < 0x20 || c > 0x7E) c = '.';
        out += c;
    }
    if (len > maxLen) out += String("...");
    return out;
}

// Match a Characteristic's (parent-service UUID, char UUID) against
// BleControlChars. Both UUIDs come in as NimBLEUUID strings; the table
// mixes 16-bit and 128-bit forms - compare structurally.
bool matchControlEntry(const NimBLEUUID& svcUuid, const NimBLEUUID& charUuid,
                       const ble_control_chars::Entry& e, String& labelOut) {
    // Service side.
    bool svcMatch = false;
    if (e.serviceUuid16) {
        svcMatch = svcUuid.equals(NimBLEUUID(e.serviceUuid16));
    } else if (e.serviceUuid128) {
        svcMatch = svcUuid.equals(NimBLEUUID(e.serviceUuid128));
    }
    if (!svcMatch) return false;
    bool charMatch = false;
    if (e.charUuid16) {
        charMatch = charUuid.equals(NimBLEUUID(e.charUuid16));
    } else if (e.charUuid128) {
        charMatch = charUuid.equals(NimBLEUUID(e.charUuid128));
    }
    if (!charMatch) return false;
    labelOut = String(ble_control_chars::kindLabel(e.kind)) + " " + e.label;
    return true;
}

}  // namespace

void BleGattClient::begin(QueueHandle_t outQueue) {
    _mutex = xSemaphoreCreateMutex();
    _outQueue = outQueue;
}

bool BleGattClient::start(const String& targetAddr) {
    if (_running || targetAddr.length() != 17) return false;
    if (_mutex && xSemaphoreTake(_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        _target = targetAddr;
        _result = WalkResult{};
        _result.targetAddr = targetAddr;
        _result.startMs = millis();
        xSemaphoreGive(_mutex);
    }
    _running = true;
    notify(ScanEventType::ScanStarted);
    xTaskCreatePinnedToCore(&BleGattClient::taskEntry, "blegatt", 6144, this, 1, nullptr, 0);
    return true;
}

void BleGattClient::stop() { _running = false; }

void BleGattClient::taskEntry(void* arg) {
    static_cast<BleGattClient*>(arg)->run();
    vTaskDelete(nullptr);
}

void BleGattClient::run() {
    // The scanner (BluetoothManager) must be stopped for the connection
    // to reliably establish - NimBLE's controller can't scan and connect
    // at the same time on ESP32. Snapshot the previous state so we can
    // resume scanning after the walk.
    bool resumeScan = g_bluetoothManager.isRunning();
    if (resumeScan) g_bluetoothManager.stop();
    // Give the scan state machine a moment to actually stop before we
    // reach for the controller.
    vTaskDelay(pdMS_TO_TICKS(200));

    // NimBLE may not have been initialized by BluetoothManager if the user
    // never opened BLE SCAN this session - initialize it here too.
    if (!NimBLEDevice::getInitialized()) {
        NimBLEDevice::init("");
        NimBLEDevice::setPower(ESP_PWR_LVL_N12);
    }

    NimBLEClient* client = NimBLEDevice::createClient();
    if (!client) {
        WalkResult r;
        r.targetAddr = _target;
        r.failureNote = "failed to allocate GATT client";
        r.endMs = millis();
        if (_mutex && xSemaphoreTake(_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            _result = r;
            xSemaphoreGive(_mutex);
        }
        notify(r.failureNote);
        _running = false;
        notify(ScanEventType::ScanFinished, 100);
        if (resumeScan) g_bluetoothManager.start();
        return;
    }
    client->setConnectTimeout(kConnectTimeoutMs / 1000);

    notify("connecting to " + _target);
    bool connected = client->connect(parseAddr(_target));

    WalkResult r;
    r.targetAddr = _target;
    r.startMs = millis() - 0;  // preserve startMs set by start()
    if (!connected) {
        r.failureNote = "connect failed (out of range / not connectable)";
        notify(r.failureNote);
    } else {
        r.connected = true;
        notify("connected, walking services");

        // Walk services.
        std::vector<NimBLERemoteService*>* svcs = client->getServices(true);
        size_t svcIdx = 0;
        if (svcs) {
            for (auto* svc : *svcs) {
                if (!_running) break;
                if (svcIdx >= kMaxServices) break;
                svcIdx++;

                Service outSvc;
                std::string uuidStr = svc->getUUID().toString();
                if (uuidStr.length() > 8) uuidStr = uuidStr.substr(4, 4);  // compress 128-bit if we can't help it
                outSvc.uuid = uuidStr.c_str();

                bool isDeviceInfo = svc->getUUID().equals(NimBLEUUID(kDeviceInfoService));
                bool isHid = svc->getUUID().equals(NimBLEUUID(kHidService));
                if (isHid) r.sawHidService = true;

                // Walk characteristics.
                std::vector<NimBLERemoteCharacteristic*>* cs = svc->getCharacteristics(true);
                size_t charIdx = 0;
                if (cs) {
                    for (auto* ch : *cs) {
                        if (!_running) break;
                        if (charIdx >= kMaxCharsPerService) break;
                        charIdx++;

                        Characteristic outCh;
                        std::string chUuid = ch->getUUID().toString();
                        if (chUuid.length() > 8) chUuid = chUuid.substr(4, 4);
                        outCh.uuid = chUuid.c_str();

                        // Properties. NimBLE surfaces these as bool getters.
                        if (ch->canRead()) outCh.access |= (uint8_t)CharAccess::Read;
                        if (ch->canWrite()) outCh.access |= (uint8_t)CharAccess::Write;
                        if (ch->canWriteNoResponse()) outCh.access |= (uint8_t)CharAccess::WriteNoResp;
                        if (ch->canNotify()) outCh.access |= (uint8_t)CharAccess::Notify;
                        if (ch->canIndicate()) outCh.access |= (uint8_t)CharAccess::Indicate;

                        bool writable = (outCh.access & ((uint8_t)CharAccess::Write |
                                                          (uint8_t)CharAccess::WriteNoResp)) != 0;
                        if (writable) r.writableCharCount++;

                        // #8 - control vector detection. Detection ONLY:
                        // we NEVER write to these characteristics. That
                        // constraint is enforced by the fact that no code
                        // path below calls ch->writeValue().
                        for (size_t k = 0; k < ble_control_chars::kCount; k++) {
                            String lbl;
                            if (matchControlEntry(svc->getUUID(), ch->getUUID(),
                                                    ble_control_chars::kEntries[k], lbl)) {
                                outCh.controlHit = true;
                                outCh.controlLabel = lbl;
                                r.knownControlCount++;
                                if (writable) r.knownControlWritable++;
                                break;
                            }
                        }

                        // Device Information Service world-readable strings
                        // (feature #2 - the "banner grab" equivalent). Only
                        // reads when the characteristic itself is Read and
                        // does NOT require encryption/auth (posture guess:
                        // truly world-readable). We deliberately don't
                        // trigger pairing to read anything - the finding of
                        // "everything else needs auth" is itself the
                        // result.
                        if (isDeviceInfo && (outCh.access & (uint8_t)CharAccess::Read)) {
                            std::string val = ch->readValue();
                            String printableVal = printable(
                                (const uint8_t*)val.data(), val.size(), kMaxReadableBytes);
                            outCh.value = printableVal;
                            if (ch->getUUID().equals(NimBLEUUID(kDiManufacturer))) r.diManufacturer = printableVal;
                            else if (ch->getUUID().equals(NimBLEUUID(kDiModel))) r.diModel = printableVal;
                            else if (ch->getUUID().equals(NimBLEUUID(kDiFirmware))) r.diFirmware = printableVal;
                            else if (ch->getUUID().equals(NimBLEUUID(kDiSerial))) r.diSerial = printableVal;
                            else if (ch->getUUID().equals(NimBLEUUID(kDiHardware))) r.diHardware = printableVal;
                        }

                        // Feature #6 posture: a writable characteristic
                        // whose CCCD read succeeded WITHOUT any pairing/
                        // auth interaction is a "just-works write" hint.
                        // We approximate this by: if we could read the
                        // characteristic's properties without ever getting
                        // an insufficient-encryption error (which NimBLE
                        // would surface via canRead() returning false when
                        // the property is present but auth-guarded), it's
                        // an unauth writable endpoint.
                        if (writable) {
                            // Best-effort - NimBLE doesn't expose per-char
                            // "read-required auth level" directly; we treat
                            // "we got here without a pairing prompt" as the
                            // proxy. Conservatively count them as no-auth
                            // writable; a real weak-pairing tool would look
                            // at the ATT permissions bitmap directly.
                            outCh.authReq = false;
                            r.writableNoAuthCount++;
                        }

                        outSvc.chars.push_back(outCh);
                    }
                }
                r.services.push_back(outSvc);
            }
        }
        r.disconnected = client->disconnect() == 0;
        notify("walk done, disconnected");
    }
    r.endMs = millis();

    NimBLEDevice::deleteClient(client);

    if (_mutex && xSemaphoreTake(_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        // Preserve the original startMs the caller stamped.
        uint32_t start = _result.startMs;
        _result = r;
        _result.startMs = start;
        xSemaphoreGive(_mutex);
    }

    _running = false;
    notify(ScanEventType::ScanFinished, 100);

    if (resumeScan) g_bluetoothManager.start();
}

String BleGattClient::target() const {
    if (!_mutex || xSemaphoreTake(_mutex, pdMS_TO_TICKS(50)) != pdTRUE) return String();
    String t = _target;
    xSemaphoreGive(_mutex);
    return t;
}

BleGattClient::WalkResult BleGattClient::result() const {
    if (!_mutex || xSemaphoreTake(_mutex, pdMS_TO_TICKS(100)) != pdTRUE) return WalkResult{};
    WalkResult r = _result;
    xSemaphoreGive(_mutex);
    return r;
}

void BleGattClient::notify(const String& text) {
    if (!_outQueue) return;
    ScanNotification n;
    n.source = ScanSource::BleGatt;
    n.type = ScanEventType::LogLine;
    n.setText(text.c_str());
    xQueueSend(_outQueue, &n, 0);
}

void BleGattClient::notify(ScanEventType type, uint8_t pct) {
    if (!_outQueue) return;
    ScanNotification n;
    n.source = ScanSource::BleGatt;
    n.type = type;
    n.progressPct = pct;
    xQueueSend(_outQueue, &n, 0);
}
