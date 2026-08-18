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
        _tracked.clear();
        _floodBssids.clear();
        if (xSemaphoreTake(_mutex, portMAX_DELAY) == pdTRUE) {
            _events.clear();
            xSemaphoreGive(_mutex);
        }
        _newDeviceCount = 0;
        _goneDeviceCount = 0;
        _floodCount = 0;
        _cyclesRun = 0;
        _capturedFrames = 0;
        _pcapPartCount = 0;
        _sessionStartMs = millis();

        _pcapBase = netrunner::reportBase(fs, _network);
        openPcapPart();

        _captureQueue = xQueueCreate(kCaptureQueueDepth, sizeof(CapturedFrame));

        esp_wifi_set_promiscuous_rx_cb(&SentinelManager::promiscuousRxTrampoline);
        esp_wifi_set_promiscuous(true);
        notify("sentinel: watching " + _network);

        // Force the first discovery cycle to fire immediately rather
        // than waiting out a full kScanIntervalMs from a fresh boot.
        uint32_t lastScanMs = millis() - kScanIntervalMs;
        bool waitingForScan = false;
        uint32_t floodWindowStartMs = millis();

        CapturedFrame frame;
        while (_running) {
            for (uint8_t i = 0; i < kDrainPerTick && _captureQueue && xQueueReceive(_captureQueue, &frame, 0) == pdTRUE;
                 i++) {
                _capturedFrames++;
                if (_pcapFile) {
                    pcap::writeRecord(_pcapFile, frame.data, frame.capturedLen, frame.originalLen);
                    _currentFileBytes += 16u + frame.capturedLen;  // 16 = pcap per-record header size
                    if (_currentFileBytes >= kMaxPcapBytes) openPcapPart();
                }
            }

            if (millis() - floodWindowStartMs >= kFloodWindowMs) {
                rollFloodWindow();
                floodWindowStartMs = millis();
            }

            if (g_wifi.isConnected()) {
                if (waitingForScan) {
                    if (!g_scanManager.isRunning()) {
                        waitingForScan = false;
                        checkForNewDevices();
                        checkForGoneDevices();
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
        if (_pcapFile) _pcapFile.close();
        if (_captureQueue) {
            vQueueDelete(_captureQueue);
            _captureQueue = nullptr;
        }

        writeSummary();

        notify("sentinel stopped: " + String((unsigned)_capturedFrames) + "f " + String((unsigned)_newDeviceCount) +
               "new");
        // Deliberately doesn't call g_wifi.autoConnect() - unlike
        // BeaconProbeSniffer/DeauthManager/PmkidManager, this never
        // changes channel or leaves the STA connection, so there's
        // nothing to reconnect.
    }
}

bool SentinelManager::openPcapPart() {
    if (_pcapFile) _pcapFile.close();

    fs::FS& fs = sdcard::exportFs();
    uint16_t partNum = ++_pcapPartCount;
    _pcapPath = _pcapBase + "_p" + String(partNum) + ".pcap";
    _pcapFile = fs.open(_pcapPath, "w");
    _currentFileBytes = 0;
    if (!_pcapFile) return false;

    pcap::writeGlobalHeader(_pcapFile);
    _currentFileBytes = 24;  // pcap global header size
    return true;
}

void SentinelManager::promiscuousRxTrampoline(void* buf, wifi_promiscuous_pkt_type_t type) {
    if (type != WIFI_PKT_DATA && type != WIFI_PKT_MGMT) return;
    auto* pkt = static_cast<wifi_promiscuous_pkt_t*>(buf);
    if (!pkt) return;
    g_sentinelManager.onCapturedFrame(pkt->payload, pkt->rx_ctrl.sig_len);
}

void SentinelManager::onCapturedFrame(const uint8_t* p, uint16_t len) {
    if (len == 0) return;

    // Folded-in GUARD MODE logic - see class comment on why this isn't
    // a separate promiscuous session sharing (and fighting over) the
    // same callback slot.
    checkDeauthFlood(p, len);

    if (!_captureQueue) return;
    CapturedFrame frame;
    frame.originalLen = len;
    frame.capturedLen = (len > sizeof(frame.data)) ? (uint16_t)sizeof(frame.data) : len;
    memcpy(frame.data, p, frame.capturedLen);
    xQueueSend(_captureQueue, &frame, 0);  // best-effort - a full queue just drops this frame, see class comment
}

void SentinelManager::checkDeauthFlood(const uint8_t* p, uint16_t len) {
    if (len < 24) return;

    uint8_t fc0 = p[0];
    uint8_t type = (fc0 >> 2) & 0x3;
    uint8_t subtype = (fc0 >> 4) & 0xF;
    if (type != 0) return;                      // not Management
    if (subtype != 12 && subtype != 10) return;  // 12 = Deauthentication, 10 = Disassociation

    // Addr3 carries the BSSID regardless of direction, same reasoning
    // as DeauthWatcher::onManagementFrame.
    String bssidStr = macToString(p + 16);

    bool becameFlood = false;
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        FloodBssid* existing = nullptr;
        for (auto& fb : _floodBssids) {
            if (fb.bssid == bssidStr) {
                existing = &fb;
                break;
            }
        }
        if (!existing && _floodBssids.size() < kMaxFloodBssids) {
            FloodBssid fb;
            fb.bssid = bssidStr;
            _floodBssids.push_back(fb);
            existing = &_floodBssids.back();
        }
        if (existing) {
            existing->windowCount++;
            if (!existing->flooding && existing->windowCount >= kFloodThreshold) {
                existing->flooding = true;
                becameFlood = true;
            }
        }
        xSemaphoreGive(_mutex);
    }

    if (becameFlood) {
        Event ev;
        ev.kind = EventKind::DeauthFlood;
        ev.mac = bssidStr;  // BSSID, for this event kind
        ev.atMs = millis();
        addEvent(ev);
        _floodCount++;
        sound::playCredAlert();  // same urgency tier as a possible evil twin
        notify("deauth flood: " + bssidStr);
    }
}

void SentinelManager::rollFloodWindow() {
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        for (auto& fb : _floodBssids) {
            if (fb.windowCount < kFloodThreshold) fb.flooding = false;  // rate dropped back below threshold
            fb.windowCount = 0;
        }
        xSemaphoreGive(_mutex);
    }
}

void SentinelManager::checkForNewDevices() {
    uint32_t thisCycle = _cyclesRun + 1;  // _cyclesRun itself is bumped by the caller after both passes

    size_t n = g_scanManager.hostCount();
    HostInfo h;
    for (size_t i = 0; i < n; i++) {
        if (!g_scanManager.getHost(i, h) || !h.alive || !h.macKnown) continue;
        String mac = macToString(h.mac);

        // Presence tracking for checkForGoneDevices() - every alive host
        // feeds this, not just newly-flagged ones.
        TrackedHost* t = nullptr;
        for (auto& th : _tracked) {
            if (th.mac == mac) {
                t = &th;
                break;
            }
        }
        if (t) {
            t->lastIp = h.ip;
            t->hostname = h.hostname;
            t->vendor = h.vendor;
            t->lastSeenCycle = thisCycle;
            t->gone = false;
        } else if (_tracked.size() < kMaxTrackedHosts) {
            TrackedHost th;
            th.mac = mac;
            th.lastIp = h.ip;
            th.hostname = h.hostname;
            th.vendor = h.vendor;
            th.lastSeenCycle = thisCycle;
            _tracked.push_back(th);
        }

        bool known = false;
        for (const auto& m : _knownMacs) {
            if (m == mac) {
                known = true;
                break;
            }
        }
        if (known) continue;

        _knownMacs.push_back(mac);  // never re-flag this device for the rest of this session

        Event ev;
        ev.kind = EventKind::NewDevice;
        ev.ip = h.ip;
        ev.mac = mac;
        ev.hostname = h.hostname;
        ev.vendor = h.vendor;
        ev.atMs = millis();
        addEvent(ev);
        _newDeviceCount++;

        sound::playAlert();
        notify("new device: " + (h.hostname.length() ? h.hostname : h.ip.toString()));
    }
}

void SentinelManager::checkForGoneDevices() {
    uint32_t thisCycle = _cyclesRun + 1;

    for (auto& t : _tracked) {
        if (t.gone) continue;                    // already flagged - don't re-flag every subsequent miss
        if (t.lastSeenCycle == thisCycle) continue;  // seen this cycle, present
        if (thisCycle - t.lastSeenCycle < kMissedCyclesThreshold) continue;

        t.gone = true;

        Event ev;
        ev.kind = EventKind::DeviceGone;
        ev.ip = t.lastIp;
        ev.mac = t.mac;
        ev.hostname = t.hostname;
        ev.vendor = t.vendor;
        ev.atMs = millis();
        addEvent(ev);
        _goneDeviceCount++;

        sound::playAlert();
        notify("device gone: " + (t.hostname.length() ? t.hostname : t.lastIp.toString()));
    }
}

void SentinelManager::writeSummary() {
    fs::FS& fs = sdcard::exportFs();
    File f = fs.open(_pcapBase + "_summary.txt", "w");
    if (!f) return;

    uint32_t durationSec = (millis() - _sessionStartMs) / 1000;

    uint32_t cyclesRun = _cyclesRun;
    uint32_t framesCaptured = _capturedFrames;
    uint16_t pcapParts = _pcapPartCount;
    uint32_t newCount = _newDeviceCount;
    uint32_t goneCount = _goneDeviceCount;
    uint32_t floodCount = _floodCount;

    f.println("SENTINEL MODE session summary");
    f.print("network: ");
    f.println(_network);
    f.print("duration: ");
    f.print(durationSec / 60);
    f.print("m ");
    f.print(durationSec % 60);
    f.println("s");
    f.print("cycles run: ");
    f.println(cyclesRun);
    f.print("frames captured: ");
    f.println(framesCaptured);
    f.print("pcap parts: ");
    f.println(pcapParts);
    for (uint16_t i = 1; i <= pcapParts; i++) {
        f.print("  ");
        f.println(_pcapBase + "_p" + String(i) + ".pcap");
    }
    f.println();

    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(500)) == pdTRUE) {
        f.print("new devices: ");
        f.println(newCount);
        for (const auto& ev : _events) {
            if (ev.kind != EventKind::NewDevice) continue;
            f.print("  ");
            f.print(ev.ip.toString());
            f.print(" ");
            f.print(ev.mac);
            f.print(" ");
            f.print(ev.hostname.length() ? ev.hostname : String("-"));
            f.print(" ");
            f.println(ev.vendor.length() ? ev.vendor : String("unknown"));
        }
        f.println();

        f.print("devices gone: ");
        f.println(goneCount);
        for (const auto& ev : _events) {
            if (ev.kind != EventKind::DeviceGone) continue;
            f.print("  ");
            f.print(ev.ip.toString());
            f.print(" ");
            f.print(ev.mac);
            f.print(" ");
            f.println(ev.hostname.length() ? ev.hostname : String("-"));
        }
        f.println();

        f.print("deauth floods: ");
        f.println(floodCount);
        for (const auto& ev : _events) {
            if (ev.kind != EventKind::DeauthFlood) continue;
            f.print("  ");
            f.println(ev.mac);  // BSSID, for this event kind
        }

        xSemaphoreGive(_mutex);
    }

    f.close();
}

void SentinelManager::addEvent(const Event& ev) {
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
        if (_events.size() >= kMaxEvents) _events.erase(_events.begin());
        _events.push_back(ev);
        xSemaphoreGive(_mutex);
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

size_t SentinelManager::eventLogCount() const {
    if (!_mutex || xSemaphoreTake(_mutex, pdMS_TO_TICKS(100)) != pdTRUE) return 0;
    size_t n = _events.size();
    xSemaphoreGive(_mutex);
    return n;
}

bool SentinelManager::getEvent(size_t index, Event& out) const {
    if (!_mutex || xSemaphoreTake(_mutex, pdMS_TO_TICKS(100)) != pdTRUE) return false;
    bool ok = index < _events.size();
    if (ok) out = _events[_events.size() - 1 - index];
    xSemaphoreGive(_mutex);
    return ok;
}
