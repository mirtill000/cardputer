#include "SentinelManager.h"
#include "ScanManager.h"
#include "../core/Types.h"
#include "../net/WifiManager.h"
#include "../storage/ScanHistory.h"
#include "../storage/SdCard.h"
#include "../storage/NetrunnerPaths.h"
#include "../storage/PcapWriter.h"
#include "../ui/Sound.h"
#include <cstring>

SentinelManager g_sentinelManager;

void SentinelManager::begin(QueueHandle_t outQueue) {
    _mutex = xSemaphoreCreateMutex();
    _outQueue = outQueue;
    // Permanent-task-that-idles pattern, same as WardrivingManager/
    // CdpLldpSniffer/BeaconProbeSniffer/DeauthWatcher.
    xTaskCreatePinnedToCore(&SentinelManager::taskEntry, "sentinel", 8192, this, 1, nullptr, 0);
}

bool SentinelManager::start() {
    if (_running) return false;
    if (!g_wifi.isConnected()) return false;
    _running = true;
    return true;
}

void SentinelManager::stop() { _running = false; }

void SentinelManager::taskEntry(void* arg) {
    static_cast<SentinelManager*>(arg)->run();
}

void SentinelManager::run() {
    for (;;) {
        if (!_running) {
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }

        // --- session setup ---
        _network = g_wifi.currentSsid();
        fs::FS& fs = sdcard::exportFs();

        _knownMacs.clear();
        ScanHistory::loadKnownMacs(fs, _network, _knownMacs);
        if (xSemaphoreTake(_mutex, portMAX_DELAY) == pdTRUE) {
            _newDevices.clear();
            xSemaphoreGive(_mutex);
        }
        _newDeviceCount = 0;
        _cyclesRun = 0;
        _capturedFrames = 0;

        _pcapPath = netrunner::reportBase(fs, _network) + ".pcap";
        File pcapFile = fs.open(_pcapPath, "w");
        bool haveFile = (bool)pcapFile;
        if (haveFile) pcap::writeGlobalHeader(pcapFile);

        _captureQueue = xQueueCreate(kCaptureQueueDepth, sizeof(CapturedFrame));

        esp_wifi_set_promiscuous_rx_cb(&SentinelManager::promiscuousRxTrampoline);
        esp_wifi_set_promiscuous(true);
        notify("sentinel: watching " + _network);

        // Force the first discovery cycle to fire immediately rather
        // than waiting out a full kScanIntervalMs from a fresh boot.
        uint32_t lastScanMs = millis() - kScanIntervalMs;
        bool waitingForScan = false;

        CapturedFrame frame;
        while (_running) {
            for (uint8_t i = 0; i < kDrainPerTick && _captureQueue && xQueueReceive(_captureQueue, &frame, 0) == pdTRUE;
                 i++) {
                _capturedFrames++;
                if (haveFile) pcap::writeRecord(pcapFile, frame.data, frame.capturedLen, frame.originalLen);
            }

            if (g_wifi.isConnected()) {
                if (waitingForScan) {
                    if (!g_scanManager.isRunning()) {
                        waitingForScan = false;
                        checkForNewDevices();
                        _cyclesRun++;
                        lastScanMs = millis();
                    }
                } else if (millis() - lastScanMs >= kScanIntervalMs) {
                    if (!g_scanManager.isRunning()) {
                        g_scanManager.startDiscoveryScan();
                        waitingForScan = true;
                    } else {
                        lastScanMs = millis();  // something else is scanning - just retry next interval
                    }
                }
            }

            vTaskDelay(pdMS_TO_TICKS(100));
        }

        esp_wifi_set_promiscuous(false);
        if (haveFile) pcapFile.close();
        if (_captureQueue) {
            vQueueDelete(_captureQueue);
            _captureQueue = nullptr;
        }

        notify("sentinel stopped: " + String((unsigned)_capturedFrames) + "f " + String((unsigned)_newDeviceCount) +
               "new");
        // Deliberately doesn't call g_wifi.autoConnect() - unlike
        // BeaconProbeSniffer/DeauthManager/PmkidManager, this never
        // changes channel or leaves the STA connection, so there's
        // nothing to reconnect.
    }
}

void SentinelManager::promiscuousRxTrampoline(void* buf, wifi_promiscuous_pkt_type_t type) {
    if (type != WIFI_PKT_DATA && type != WIFI_PKT_MGMT) return;
    auto* pkt = static_cast<wifi_promiscuous_pkt_t*>(buf);
    if (!pkt) return;
    g_sentinelManager.onCapturedFrame(pkt->payload, pkt->rx_ctrl.sig_len);
}

void SentinelManager::onCapturedFrame(const uint8_t* p, uint16_t len) {
    if (!_captureQueue || len == 0) return;

    CapturedFrame frame;
    frame.originalLen = len;
    frame.capturedLen = (len > sizeof(frame.data)) ? (uint16_t)sizeof(frame.data) : len;
    memcpy(frame.data, p, frame.capturedLen);
    xQueueSend(_captureQueue, &frame, 0);  // best-effort - a full queue just drops this frame, see class comment
}

void SentinelManager::checkForNewDevices() {
    size_t n = g_scanManager.hostCount();
    HostInfo h;
    for (size_t i = 0; i < n; i++) {
        if (!g_scanManager.getHost(i, h) || !h.alive || !h.macKnown) continue;
        String mac = macToString(h.mac);

        bool known = false;
        for (const auto& m : _knownMacs) {
            if (m == mac) {
                known = true;
                break;
            }
        }
        if (known) continue;

        _knownMacs.push_back(mac);  // never re-flag this device for the rest of this session

        NewDevice nd;
        nd.ip = h.ip;
        nd.mac = mac;
        nd.hostname = h.hostname;
        nd.vendor = h.vendor;
        nd.seenAtMs = millis();

        if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
            if (_newDevices.size() >= kMaxNewDevices) _newDevices.erase(_newDevices.begin());
            _newDevices.push_back(nd);
            xSemaphoreGive(_mutex);
        }
        _newDeviceCount++;

        sound::playAlert();
        notify("new device: " + (h.hostname.length() ? h.hostname : nd.ip.toString()));
    }
}

void SentinelManager::notify(const String& text) {
    if (!_outQueue) return;
    ScanNotification n;
    n.source = ScanSource::Sentinel;
    n.type = ScanEventType::LogLine;
    n.setText(text.c_str());
    xQueueSend(_outQueue, &n, 0);
}

size_t SentinelManager::newDeviceLogCount() const {
    if (!_mutex || xSemaphoreTake(_mutex, pdMS_TO_TICKS(100)) != pdTRUE) return 0;
    size_t n = _newDevices.size();
    xSemaphoreGive(_mutex);
    return n;
}

bool SentinelManager::getNewDevice(size_t index, NewDevice& out) const {
    if (!_mutex || xSemaphoreTake(_mutex, pdMS_TO_TICKS(100)) != pdTRUE) return false;
    bool ok = index < _newDevices.size();
    if (ok) out = _newDevices[_newDevices.size() - 1 - index];
    xSemaphoreGive(_mutex);
    return ok;
}
