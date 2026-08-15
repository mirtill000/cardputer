#pragma once

#include <Arduino.h>
#include <IPAddress.h>
#include <atomic>
#include <vector>
#include "../core/EventQueue.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include <esp_wifi.h>

// "SENTINEL MODE": stand-alone continuous guard for the network this
// device is already connected to (not a survey of everything nearby the
// way WAR DRIVING/BEACON-PROBE INTEL are) - two things run together for
// as long as it's on:
//
//  1. Re-runs NETWORK SCAN's own discovery sweep (g_scanManager) every
//     kScanIntervalMs and flags any host whose MAC has never appeared in
//     the last kMaxEntries snapshots of THIS network (see
//     storage/ScanHistory.h's loadKnownMacs - same "known device"
//     baseline HOST LIST's never-seen-before flag already uses) as a
//     new device: a short alert tone plus a log entry with its IP/MAC/
//     hostname/vendor. Deliberately does NOT call
//     ScanHistory::saveSnapshot() every cycle - that would flood the
//     shared kMaxEntries=20-slot rolling history with near-duplicate
//     snapshots taken 30s apart and shrink the "known device" baseline's
//     real time window from however long HOST LIST has been used
//     manually down to just ~10 minutes, the opposite of what a
//     standing baseline is for. The baseline is loaded once at start()
//     and then only grown in memory for the rest of this session.
//  2. Captures every management/data 802.11 frame seen on the current
//     channel (frame HEADERS are always readable in promiscuous mode;
//     payload content stays exactly as encrypted-or-not as it already
//     was - this firmware never attempts to decrypt anything) to a
//     .pcap file under /netrunner, for offline analysis in Wireshark/
//     tshark - a running traffic log of who's talking and how much,
//     not a way to read anyone's actual data. Stays on the already-
//     associated channel, same "don't hop, don't disrupt your own STA
//     connection" choice as CdpLldpSniffer/RogueDhcpDetector/
//     PassiveHostDiscovery/DeauthWatcher - unlike those, this ALSO
//     captures DATA frames, not just management, since a genuine
//     traffic dump needs both.
//
// Best-effort like every other capture in this firmware: frames are
// truncated to kCaptureLen bytes and the capture queue drops frames
// under load rather than blocking - see PcapWriter.h and
// ArpSpoofManager.h's RISK block for the general shape of that
// tradeoff. The pcap file grows for as long as this runs with no
// automatic rotation or size cap - a long session on a busy network can
// use meaningful SD space; stop() when you're done watching.
//
// Shares the single esp_wifi promiscuous callback with every other
// promiscuous consumer in this firmware (see ui/ActivityStatus.h) -
// running this alongside ARP/MITM, deauth, PMKID, CDP/LLDP, rogue DHCP,
// passive host discovery, beacon/probe intel or GUARD MODE means
// whichever started second silently steals the other's frames.
class SentinelManager {
public:
    struct NewDevice {
        IPAddress ip;
        String mac;
        String hostname;
        String vendor;
        uint32_t seenAtMs = 0;
    };

    void begin(QueueHandle_t outQueue);

    // False if WiFi isn't connected, or already running. Watches
    // whatever network is connected AT THIS MOMENT - if it changes
    // mid-session (this device reconnects elsewhere), Sentinel keeps
    // running against the new network's traffic but the "known device"
    // baseline stays the one loaded from the network active at start().
    bool start();
    void stop();
    bool isRunning() const { return _running; }

    String network() const { return _network; }
    uint32_t cyclesRun() const { return _cyclesRun; }
    uint32_t newDeviceCount() const { return _newDeviceCount; }
    uint32_t capturedFrames() const { return _capturedFrames; }
    String pcapPath() const { return _pcapPath; }

    size_t newDeviceLogCount() const;
    bool getNewDevice(size_t index, NewDevice& out) const;  // most-recently-seen first

private:
    static constexpr uint32_t kScanIntervalMs = 30000;
    static constexpr size_t kMaxNewDevices = 30;
    static constexpr uint8_t kCaptureQueueDepth = 24;
    static constexpr uint8_t kCaptureLen = 256;       // truncation length, same as DeauthManager/PmkidManager
    static constexpr uint8_t kDrainPerTick = 8;        // capped so a busy network can't starve the scan-cycle logic

    struct CapturedFrame {
        uint8_t data[kCaptureLen];
        uint16_t capturedLen = 0;
        uint16_t originalLen = 0;
    };

    static void taskEntry(void* arg);
    void run();
    static void promiscuousRxTrampoline(void* buf, wifi_promiscuous_pkt_type_t type);
    void onCapturedFrame(const uint8_t* p, uint16_t len);
    void checkForNewDevices();
    void notify(const String& text);

    mutable SemaphoreHandle_t _mutex = nullptr;
    std::vector<String> _knownMacs;      // baseline (loaded once) + session-confirmed - sentinel task only, no mutex needed
    std::vector<NewDevice> _newDevices;  // read by the UI - mutex-protected
    QueueHandle_t _outQueue = nullptr;
    QueueHandle_t _captureQueue = nullptr;

    std::atomic<bool> _running{false};
    std::atomic<uint32_t> _cyclesRun{0};
    std::atomic<uint32_t> _newDeviceCount{0};
    std::atomic<uint32_t> _capturedFrames{0};
    String _network;
    String _pcapPath;
};

extern SentinelManager g_sentinelManager;
