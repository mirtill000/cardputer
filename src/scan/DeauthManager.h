#pragma once

#include <IPAddress.h>
#include <atomic>
#include "../core/EventQueue.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

// Single-target WPA handshake capture: a small, fixed burst of
// deauthentication frames against ONE client on ONE access point (never
// a loop, never "all clients", never a bare BSSID with no specific
// client picked) to force a re-association, followed by a short bounded
// promiscuous capture window that dumps every 802.11 frame involving
// that AP/client pair to a standard .pcap file on SD for OFFLINE
// analysis (Wireshark/aircrack-ng/hashcat) — this firmware never
// attempts to parse or crack the handshake itself.
//
// This is the single most disruptive technique in the whole firmware —
// unlike everything else here (which only ever touches services/hosts
// already discovered, or a lone ARP entry on one host), a deauth frame
// forcibly and immediately drops a real client's WiFi connection the
// instant it arrives, with no possible "opt out" on that client's side.
// The scope cuts below are deliberate safety rails, not just style:
//  - exactly one client MAC, typed/picked explicitly — never a
//    broadcast/"all clients" option exists anywhere in this class or
//    its UI.
//  - a small FIXED frame count (kDeauthBurst) per run — no loop, no
//    "repeat every N seconds" mode. Re-running requires the user to
//    explicitly start a new session again from DeauthScreen.
//  - the capture window (kCaptureWindowMs) is hard-bounded and always
//    ends on its own even if nothing useful was captured.
//
// RISK: uses esp_wifi_80211_tx() for raw management-frame injection —
// declared in esp_wifi.h on some esp-idf releases, moved to the
// "private" esp_private/wifi.h on others (still exported, just not
// advertised as public API). See the __has_include guard in the .cpp.
// Shares ArpSpoofManager's promiscuous-capture machinery/uncertainty
// (see its header) for the handshake-capture side, but is meaningfully
// SAFER on the parsing front: this class never parses captured frame
// contents at all, just writes their raw bytes into a pcap file
// verbatim — the one piece of format here that has to be exactly right
// is the pcap file layout itself, a small, fixed, unambiguous byte
// layout, not 802.11/IP/TCP semantics.
class DeauthManager {
public:
    void begin(QueueHandle_t outQueue);

    // apBssid/clientMac: "aa:bb:cc:dd:ee:ff" format, same as
    // WifiManager::ScanResult::bssid. channel: the AP's WiFi channel
    // (from its WAR DRIVING sighting). No-op if already running or if
    // either MAC fails to parse.
    bool start(const String& apBssid, uint8_t channel, const String& clientMac);
    bool isRunning() const { return _running; }

    uint32_t framesSent() const { return _framesSent; }
    uint32_t capturedPackets() const { return _captured; }
    String pcapPath() const { return _pcapPath; }

private:
    static constexpr uint8_t kDeauthBurst = 4;           // 2 each direction — see class comment
    static constexpr uint32_t kCaptureWindowMs = 10000;  // hard-bounded, always ends on its own
    static constexpr uint8_t kCaptureQueueDepth = 16;

    struct CapturedFrame {
        uint8_t data[256];
        uint16_t capturedLen = 0;  // how many bytes of data[] are valid (pcap incl_len)
        uint16_t originalLen = 0;  // the real over-the-air frame length (pcap orig_len)
    };

    static void taskEntry(void* arg);
    void run();
    bool sendDeauth(const uint8_t dst[6], const uint8_t src[6], const uint8_t bssid[6]);
    static void promiscuousRxTrampoline(void* buf, int type);
    void onCapturedFrame(const uint8_t* payload, uint16_t len);
    void notify(const String& text);

    QueueHandle_t _outQueue = nullptr;
    QueueHandle_t _captureQueue = nullptr;

    std::atomic<bool> _running{false};
    uint8_t _apBssid[6] = {0};
    uint8_t _clientMac[6] = {0};
    uint8_t _channel = 1;
    std::atomic<uint32_t> _framesSent{0};
    std::atomic<uint32_t> _captured{0};
    String _pcapPath;
};

extern DeauthManager g_deauthManager;

// Parses "aa:bb:cc:dd:ee:ff" into 6 raw bytes. Shared by DeauthScreen
// (client MAC entry) and DeauthManager itself.
bool parseMacString(const String& s, uint8_t out[6]);
