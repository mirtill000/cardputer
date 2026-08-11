#pragma once

#include <IPAddress.h>
#include <atomic>
#include <vector>
#include "../core/EventQueue.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
// For wifi_promiscuous_pkt_type_t only, needed to declare
// promiscuousRxTrampoline() with the EXACT signature
// esp_wifi_set_promiscuous_rx_cb() requires (wifi_promiscuous_cb_t is a
// plain C function pointer type - the second parameter must match
// precisely, "int" doesn't implicitly convert, confirmed by a real
// build failure - see git log).
#include <esp_wifi.h>

// Single-target ARP spoofing + passive traffic-analysis session ("MITM
// AUDIT" in the UI) — the offensive sibling of WardrivingManager's
// passive listening, and the most technically involved code in this
// entire project (see the RISK block below).
//
// Scope, deliberately narrow:
//  - ONE explicit target host at a time (never subnet-wide) — picked
//    from ScanManager's already-discovered host table, same pattern as
//    CredAuditManager/PortScanManager.
//  - One-directional poisoning only: periodically sends the TARGET a
//    forged ARP reply claiming "the gateway is at my MAC", never
//    poisons the gateway's own cache about the target. This bounds the
//    blast radius of a bug to this one target's ability to reach ONLY
//    the gateway — not the whole LAN, and not the target's ability to
//    be reached BY anything else.
//  - NO packet relay/forwarding. This is the single biggest scope cut
//    from a "real" MITM tool, made deliberately: correctly relaying
//    arbitrary intercepted IP traffic (rewriting the L2 destination on
//    every captured frame and re-injecting it without ever looping,
//    dropping, or corrupting one) is an enormous, easy-to-get-wrong
//    systems task — getting it wrong doesn't just fail to compile, it
//    silently breaks the target's actual internet access for as long
//    as the session runs, turning an audit into an unintended DoS
//    against a real device. Without relay: on an ENCRYPTED (WPA2/WPA3)
//    network this tool can prove whether ARP poisoning took hold (i.e.
//    whether the network has Dynamic ARP Inspection / equivalent
//    protection), but can't read the target's traffic content, since
//    frames the target sends "to the gateway" (now us) are logged, not
//    decrypted or forwarded anywhere. On an OPEN (unencrypted) network,
//    promiscuous capture alone already sees everyone's traffic in
//    cleartext with no spoofing needed at all — that's where the
//    traffic-analysis/cookie-sniffing side of this class is actually
//    most useful. See analyzeFrame() in the .cpp.
//
// RISK — by a wide margin the least-verified code in this whole
// project, well past BLE (which only ever risked "doesn't compile"):
// this hand-parses raw 802.11 frames (frame control, address fields
// depending on ToDS/FromDS, optional QoS control, LLC/SNAP, IPv4, TCP/
// UDP) byte-by-byte with zero ability to test against real captured
// traffic before a real build. Every parsing step is written to fail
// closed (bounds-checked, silently skips anything that doesn't match
// the expected shape) rather than assume success — specifically so a
// parsing bug degrades to "this one frame gets ignored", never to
// garbage data being acted on or re-transmitted. Frames on encrypted
// networks are detected via the Protected Frame bit and never
// content-scanned — the encrypted bytes would just be noise. Uses only
// public, documented esp-idf WiFi-driver APIs (esp_wifi_set_promiscuous/
// esp_wifi_set_promiscuous_rx_cb, wifi_promiscuous_pkt_t) rather than
// private/internal ones — the same class of API used by every published
// ESP32 WiFi-sniffer example, including Espressif's own — so the
// biggest open question isn't "does this API exist" so much as "did the
// hand-rolled 802.11/IP parsing above get every offset right".
class ArpSpoofManager {
public:
    struct LogEntry {
        String text;
        uint32_t atMs = 0;
    };

    void begin(QueueHandle_t outQueue);

    // Starts spoofing `target` (must already be in ScanManager's host
    // table with a known MAC — run NETWORK SCAN first) for at most
    // `durationS` seconds (hard-capped at kMaxDurationS regardless of
    // what's asked for). No-op if already running.
    bool start(const IPAddress& target, uint16_t durationS, bool sniffTraffic);
    void stop();  // always restores the target's ARP cache before returning
    bool isRunning() const { return _running; }

    IPAddress target() const { return _target; }
    uint32_t poisonPacketsSent() const { return _poisonSent; }
    uint32_t secondsRemaining() const;

    size_t logCount() const;
    bool getLogEntry(size_t index, LogEntry& out) const;  // most-recent-first

    static constexpr uint16_t kMaxDurationS = 600;  // hard cap, ~10 minutes

private:
    static constexpr uint32_t kPoisonIntervalMs = 1500;
    static constexpr size_t kMaxLogEntries = 100;

    static void taskEntry(void* arg);
    void run();
    bool sendArpReply(const uint8_t dstMac[6], const IPAddress& dstIp, const IPAddress& claimedIp,
                       const uint8_t claimedMac[6]);
    void restoreTarget();  // sends a correcting ARP reply with the REAL gateway MAC

    static void promiscuousRxTrampoline(void* buf, wifi_promiscuous_pkt_type_t type);
    void onPromiscuousFrame(const uint8_t* payload, uint16_t len);
    void analyzeFrame(const uint8_t* pkt, uint16_t ipOffset, uint16_t len, const uint8_t srcMac[6]);
    void maybeSpoofDns(const uint8_t* udpPayload, uint16_t udpLen, const IPAddress& queryFromIp,
                        const uint8_t queryFromMac[6], uint16_t queryFromPort);
    void log(const String& text);
    void notify(const String& text);
    void notify(ScanEventType type, uint8_t pct = 0);

    mutable SemaphoreHandle_t _mutex = nullptr;
    std::vector<LogEntry> _log;
    QueueHandle_t _outQueue = nullptr;

    std::atomic<bool> _running{false};
    IPAddress _target;
    uint8_t _targetMac[6] = {0};
    IPAddress _gateway;
    uint8_t _gatewayMac[6] = {0};
    uint8_t _selfMac[6] = {0};

    uint32_t _startMs = 0;
    uint32_t _durationMs = 0;
    std::atomic<uint32_t> _poisonSent{0};
    bool _sniffTraffic = false;
};

extern ArpSpoofManager g_arpSpoofManager;

// Tiny NVS-backed DNS spoof list, edited from MitmScreen before a
// session starts: hostname -> forged IPv4 answer. Deliberately small
// (kMaxEntries) — "while sniffing this session's target's DNS queries,
// answer any match with this IP instead of leaving it to the real DNS
// server", not a general-purpose DNS server replacement.
namespace DnsSpoofList {
constexpr uint8_t kMaxEntries = 5;
uint8_t count();
String hostname(uint8_t index);
IPAddress answer(uint8_t index);
bool add(const String& host, const IPAddress& ip);
void remove(uint8_t index);
}  // namespace DnsSpoofList
