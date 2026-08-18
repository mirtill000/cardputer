#pragma once

#include <IPAddress.h>
#include <atomic>
#include <vector>
#include "../core/EventQueue.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include <esp_wifi.h>

// Passive TCP/IP stack fingerprinting, p0f-style but drastically
// simplified: listens promiscuously for TCP SYN-ACK packets and reads,
// per source host, the one signal that's genuinely reliable without a
// real signature database - initial TTL, bucketed to the nearest of
// the three values real stacks actually ship with (Linux/BSD/macOS/
// Android default to 64, Windows to 128, network gear/old Unix to
// 255) - alongside the raw window size and TCP option order it
// observed. Deliberately does NOT attempt to name a specific OS/
// version from window size + option order the way a real p0f
// signature file does: that needs thousands of verified fingerprints
// this project has no way to build or check, so the two raw fields are
// shown as-is for a human who knows p0f to read themselves, rather
// than turned into a guess with false precision. Same "read-only,
// never overclaim" ethos as UdpProbe.h's open-vs-silent honesty and
// BeaconProbeSniffer's WPS config-methods best-effort label.
//
// Kept as its own list with its own screen rather than merged into
// ScanManager's host table, same reasoning as PassiveHostDiscovery.h.
//
// Needs an in-range host to actually attempt/complete a TCP handshake
// with something while this is running - naturally happens for a PORT
// SCAN target, or any device on the LAN making its own outbound TCP
// connections. Same shared-radio caveat as every other promiscuous
// feature (see ArpSpoofManager.h's RISK block): only OPEN networks are
// readable (WPA frames are encrypted), and only one promiscuous
// callback runs at a time.
class OsFingerprint {
public:
    struct Sighting {
        IPAddress ip;
        uint8_t mac[6] = {0};
        bool macKnown = false;
        uint8_t ttl = 0;
        uint8_t ttlCeil = 0;  // ttl rounded up to 64/128/255 - see ttlGuessLabel()
        uint16_t window = 0;
        String optionOrder;  // e.g. "MSWT" (MSS/WScale/SACK/Timestamp seen, in order) - best-effort, see class comment
        uint32_t lastSeenMs = 0;
    };

    void begin(QueueHandle_t outQueue);

    void start();  // no-op if already running
    void stop();
    bool isRunning() const { return _running; }

    size_t count() const;
    bool get(size_t index, Sighting& out) const;  // most-recently-seen first

    // Human-readable label for a ttlCeil bucket (64/128/255) - the ONE
    // claim this module is willing to make outright, since it's a
    // widely-documented, stack-level default rather than a fitted
    // signature.
    static const char* ttlGuessLabel(uint8_t ttlCeil);

private:
    static constexpr size_t kMaxHosts = 40;

    static void taskEntry(void* arg);
    void run();
    static void promiscuousRxTrampoline(void* buf, wifi_promiscuous_pkt_type_t type);
    void onPromiscuousFrame(const uint8_t* payload, uint16_t len);
    void observe(const IPAddress& ip, const uint8_t mac[6], uint8_t ttl, uint16_t window, const String& optionOrder);
    void notify(const String& text);

    mutable SemaphoreHandle_t _mutex = nullptr;
    std::vector<Sighting> _hosts;
    QueueHandle_t _outQueue = nullptr;
    std::atomic<bool> _running{false};
};

extern OsFingerprint g_osFingerprint;
