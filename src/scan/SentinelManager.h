#pragma once

#include <Arduino.h>
#include <FS.h>
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
// way WAR DRIVING/BEACON-PROBE INTEL are) - four things run together for
// as long as it's on:
//
//  1. Re-runs NETWORK SCAN's own discovery sweep (g_scanManager) every
//     kScanIntervalMs and flags any host whose MAC has never appeared in
//     the last kMaxEntries snapshots of THIS network (see
//     storage/ScanHistory.h's loadKnownMacs - same "known device"
//     baseline HOST LIST's never-seen-before flag already uses) as a
//     NewDevice event: a short alert tone plus a log entry with its IP/
//     MAC/hostname/vendor. Deliberately does NOT call
//     ScanHistory::saveSnapshot() every cycle - that would flood the
//     shared kMaxEntries=20-slot rolling history with near-duplicate
//     snapshots taken 30s apart and shrink the "known device" baseline's
//     real time window from however long HOST LIST has been used
//     manually down to just ~10 minutes, the opposite of what a
//     standing baseline is for. The baseline is loaded once at start()
//     and then only grown in memory for the rest of this session.
//  2. The symmetric check: any host this session has already seen alive
//     (tracked in _tracked, built incrementally cycle by cycle - doesn't
//     have to have been flagged NewDevice first) that goes missing for
//     kMissedCyclesThreshold consecutive cycles in a row fires a
//     DeviceGone event - same alert tone, so "the printer just went
//     dark" is as visible as "a new phone joined". Re-appearing clears
//     the gone state quietly (no separate "device back" event, to avoid
//     doubling the alert noise for something that isn't itself a new
//     finding).
//  3. Counts deauth/disassoc frames per BSSID in a rolling window,
//     exactly DeauthWatcher/GUARD MODE's own logic (not literally
//     shared code - see DeauthWatcher.h for the standalone version if
//     you only want flood detection without everything else here) -
//     folded directly into the frame stream this class is already
//     reading for the traffic dump below, so a DeauthFlood event needs
//     no separate promiscuous session. This is why running GUARD MODE
//     at the same time as SENTINEL MODE is redundant, not additive: the
//     latter already does everything the former does, plus discovery
//     and the traffic dump - see ui/ActivityStatus.h, they'd fight over
//     the one promiscuous callback slot regardless.
//  4. Captures every management/data 802.11 frame seen on the current
//     channel (frame HEADERS are always readable in promiscuous mode;
//     payload content stays exactly as encrypted-or-not as it already
//     was - this firmware never attempts to decrypt anything) to .pcap
//     files under /netrunner, for offline analysis in Wireshark/tshark
//     - a running traffic log of who's talking and how much, not a way
//     to read anyone's actual data. Rotates to a new file every
//     kMaxPcapBytes so one session can't produce a single unbounded
//     file (still no cap on the TOTAL across all parts - a long session
//     on a busy network still uses meaningful SD space over time, just
//     spread across smaller files instead of one huge one).
//
// Stays on the already-associated channel the whole time (same "don't
// hop, don't disrupt your own STA connection" choice as CdpLldpSniffer/
// RogueDhcpDetector/PassiveHostDiscovery/DeauthWatcher) - unlike those,
// this ALSO captures DATA frames, not just management, since a genuine
// traffic dump needs both.
//
// When stop() is called, writes a plain-text session summary (duration,
// cycle count, every NewDevice/DeviceGone/DeauthFlood event, frame
// count, list of pcap parts) next to the pcap under /netrunner - so the
// session's findings survive after the screen moves on, without having
// to have been watching it live.
//
// Best-effort like every other capture in this firmware: frames are
// truncated to kCaptureLen bytes and the capture queue drops frames
// under load rather than blocking - see PcapWriter.h and
// ArpSpoofManager.h's RISK block for the general shape of that
// tradeoff.
//
// Shares the single esp_wifi promiscuous callback with every other
// promiscuous consumer in this firmware (see ui/ActivityStatus.h) -
// running this alongside ARP/MITM, deauth, PMKID, CDP/LLDP, rogue DHCP,
// passive host discovery, beacon/probe intel or GUARD MODE means
// whichever started second silently steals the other's frames.
class SentinelManager {
public:
    enum class EventKind : uint8_t { NewDevice, DeviceGone, DeauthFlood };

    struct Event {
        EventKind kind = EventKind::NewDevice;
        IPAddress ip;      // valid for NewDevice/DeviceGone, 0.0.0.0 for DeauthFlood
        String mac;        // MAC for NewDevice/DeviceGone, BSSID for DeauthFlood
        String hostname;   // NewDevice/DeviceGone only
        String vendor;     // NewDevice/DeviceGone only
        uint32_t atMs = 0;
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
    uint32_t goneDeviceCount() const { return _goneDeviceCount; }
    uint32_t floodCount() const { return _floodCount; }
    uint32_t capturedFrames() const { return _capturedFrames; }
    String pcapPath() const { return _pcapPath; }    // current (most recent) part
    uint16_t pcapPartCount() const { return _pcapPartCount; }

    size_t eventLogCount() const;
    bool getEvent(size_t index, Event& out) const;  // most-recently-seen first

private:
    static constexpr uint32_t kScanIntervalMs = 30000;
    static constexpr uint32_t kMissedCyclesThreshold = 2;  // consecutive missed cycles before a DeviceGone fires
    static constexpr size_t kMaxTrackedHosts = 64;          // safety cap, no PSRAM - see class comment
    static constexpr size_t kMaxEvents = 40;
    static constexpr uint8_t kCaptureQueueDepth = 24;
    static constexpr uint16_t kCaptureLen = 256;  // truncation length, same as DeauthManager/PmkidManager
    static constexpr uint8_t kDrainPerTick = 8;   // capped so a busy network can't starve the scan-cycle logic
    static constexpr uint32_t kFloodWindowMs = 10000;      // same window as DeauthWatcher
    static constexpr uint32_t kFloodThreshold = 15;         // same threshold as DeauthWatcher
    static constexpr size_t kMaxFloodBssids = 20;
    static constexpr uint32_t kMaxPcapBytes = 5 * 1024 * 1024;  // rotate to a new file past this size

    struct CapturedFrame {
        uint8_t data[kCaptureLen];
        uint16_t capturedLen = 0;
        uint16_t originalLen = 0;
    };

    struct TrackedHost {
        String mac;
        IPAddress lastIp;
        String hostname;
        String vendor;
        uint32_t lastSeenCycle = 0;  // compared against the current cycle ordinal - see checkForGoneDevices()
        bool gone = false;
    };

    struct FloodBssid {
        String bssid;
        uint32_t windowCount = 0;
        bool flooding = false;
    };

    static void taskEntry(void* arg);
    void run();
    static void promiscuousRxTrampoline(void* buf, wifi_promiscuous_pkt_type_t type);
    void onCapturedFrame(const uint8_t* p, uint16_t len);
    void checkDeauthFlood(const uint8_t* p, uint16_t len);
    void rollFloodWindow();
    void checkForNewDevices();
    void checkForGoneDevices();
    bool openPcapPart();  // (re)opens _pcapFile at a fresh part path, writes the global header
    void writeSummary();
    void addEvent(const Event& ev);
    void notify(const String& text);

    mutable SemaphoreHandle_t _mutex = nullptr;
    std::vector<String> _knownMacs;    // baseline (loaded once) + session-confirmed - sentinel task only, no mutex needed
    std::vector<TrackedHost> _tracked;  // presence tracking for DeviceGone - sentinel task only, no mutex needed
    // Touched from TWO task contexts (the promiscuous RX callback via
    // checkDeauthFlood(), and the sentinel task itself via
    // rollFloodWindow()) - unlike _knownMacs/_tracked above, this DOES
    // need _mutex.
    std::vector<FloodBssid> _floodBssids;
    std::vector<Event> _events;  // read by the UI too - mutex-protected
    QueueHandle_t _outQueue = nullptr;
    QueueHandle_t _captureQueue = nullptr;

    std::atomic<bool> _running{false};
    std::atomic<uint32_t> _cyclesRun{0};
    std::atomic<uint32_t> _newDeviceCount{0};
    std::atomic<uint32_t> _goneDeviceCount{0};
    std::atomic<uint32_t> _floodCount{0};
    std::atomic<uint32_t> _capturedFrames{0};
    std::atomic<uint16_t> _pcapPartCount{0};
    String _network;
    String _pcapBase;   // session base path (no extension), shared by every pcap part + the summary
    String _pcapPath;   // current part's full path
    File _pcapFile;
    uint32_t _currentFileBytes = 0;
    uint32_t _sessionStartMs = 0;
};

extern SentinelManager g_sentinelManager;
