#pragma once

#include <Arduino.h>
#include <atomic>
#include <vector>
#include "../core/EventQueue.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include <esp_wifi.h>
#include <WiFi.h>  // wifi_auth_mode_t

// Passive 802.11 MANAGEMENT-frame intelligence: Beacon/Probe-Response
// frames for a richer, over-the-air view of nearby APs than
// WardrivingManager's active WiFi.scanNetworks() gets (including a
// best-effort reveal of a "hidden" (non-broadcast SSID) network's real
// name once ANY frame from that BSSID carries it — a probe response
// still has to answer with the real SSID once a client that already
// knows it asks by name, which is the well-known reason SSID-hiding was
// never a real security measure), and Probe-Request frames for
// "who's nearby and what networks do they already know" — a station
// that isn't associated to anything periodically broadcasts probe
// requests, and a DIRECTED one (naming a specific SSID) is that device
// disclosing an entry from its own remembered-network list ("Preferred
// Network List"/PNL) to whoever's listening, with no authentication or
// consent step involved — this is exactly the wire behavior that made
// MAC-address randomization something OS vendors eventually shipped by
// default (see macRandomized below).
//
// NEVER TRANSMITS ANYTHING — strictly receive-only, unlike DeauthManager/
// PmkidManager/EvilTwinManager/ArpSpoofManager. Everything captured here
// (SSIDs, BSSIDs, client MACs, which SSIDs a client has probed for) is
// information devices already broadcast in the clear, unencrypted, to
// anyone with a receiver in range, by the ordinary operation of 802.11 —
// the same category of "public over-the-air broadcast" WardrivingManager
// already logs for APs with no consent gate. What's new here relative to
// that module is the CLIENT side: a probe request's source MAC (+ the
// SSIDs it names) fingerprints a specific device, not just infrastructure
// — closer to "who is nearby and what have they connected to before" than
// "what networks exist here". This is legitimate, standard site-survey/
// red-team tradecraft (exactly what Kismet/Wireshark monitor mode already
// show for free) and this firmware still touches nothing it didn't
// already have implicit permission to receive — but treat the CLIENT
// list with the same care you'd give any other export from this
// firmware: it's data about people's devices, not just APs. No disclaimer
// gate is required (see core/Config.h's offensiveEnabled comment — that
// tier is for techniques that ACT on third-party devices; this only ever
// listens), but nothing here is persisted to SD/LittleFS automatically —
// same session-only, in-RAM-only convention as CdpLldpSniffer/
// PassiveHostDiscovery, not WardrivingManager's always-on CSV log.
//
// Channel hopping: unlike CdpLldpSniffer/PassiveHostDiscovery/
// RogueDhcpDetector (which stay on whichever channel the STA already
// happens to be associated to), this module actively cycles the radio
// across every 2.4GHz channel (1-13) while running — a real beacon/probe
// survey needs to see traffic on channels the STA isn't currently on,
// the same reason WardrivingManager's underlying WiFi.scanNetworks()
// already hops internally. The unavoidable side effect: like
// DeauthManager/PmkidManager, this necessarily disrupts THIS DEVICE'S
// OWN WiFi STA connection for as long as it runs (a fixed radio can't
// stay associated to one AP's channel and hop across all of them at
// once) — self-affecting, not third-party-affecting, which is why this
// doesn't need the offensive-tools gate either; the STA reconnects on
// its own once stop() turns hopping/promiscuous mode back off (same
// g_wifi.autoConnect() nudge WardrivingManager/PmkidManager already use).
//
// Shares the single esp_wifi promiscuous callback with every other
// promiscuous consumer in this firmware (ArpSpoofManager/DeauthManager/
// PmkidManager/CdpLldpSniffer/RogueDhcpDetector/PassiveHostDiscovery/
// DeauthWatcher) — see ArpSpoofManager.h's RISK block. Running this
// alongside any of them silently starves whichever started second; see
// ui/ActivityStatus.h for the on-screen indicator.
class BeaconProbeSniffer {
public:
    struct ApBeacon {
        String ssid;        // "" while hidden and never revealed this session
        String bssid;
        int8_t rssi = 0;
        uint8_t channel = 0;  // from the DS Parameter Set IE when present, else the channel we were tuned to
        wifi_auth_mode_t encryption = WIFI_AUTH_OPEN;  // best-effort, from capability+RSN/vendor-WPA IEs — see .cpp
        String vendor;         // OUI lookup off the BSSID, best-effort
        bool hidden = false;   // true if the most recent frame from this BSSID had an empty SSID element
        bool hiddenRevealed = false;  // true once a non-empty SSID was seen for a BSSID first recorded as hidden
        // WPS (Wi-Fi Protected Setup), detection only - see findWpsIe()/
        // parseWpsAttributes() in the .cpp. This firmware never attempts
        // a PIN (Reaver/pixie-dust-style) against anything it finds here.
        bool wpsEnabled = false;
        bool wpsLocked = false;         // AP Setup Locked attribute - only meaningful when wpsEnabled
        uint16_t wpsConfigMethods = 0;  // raw Config Methods bitmask, 0 if absent/not found
        uint32_t beaconCount = 0;
        uint32_t firstSeenMs = 0;
        uint32_t lastSeenMs = 0;
    };

    struct ProbeClient {
        String mac;
        // Locally-administered bit set (IEEE 802 U/L bit) — modern iOS/
        // Android randomize the source MAC of probe requests sent while
        // not associated to anything specifically to defeat tracking like
        // this. When true, `vendor` is deliberately left blank (a
        // randomized MAC's "OUI" bytes carry no real manufacturer
        // information) and this client's identity won't survive a MAC
        // change — the SSIDs it revealed while using this MAC are still
        // real, just not attributable to the same device for very long.
        bool macRandomized = false;
        String vendor;
        // Directed probes only — a wildcard probe (empty SSID element,
        // "is anybody there") reveals nothing and is never added here.
        // Capped per-client (kMaxProbedSsidsPerClient) — this is a PNL
        // sample, not a guarantee of completeness.
        std::vector<String> probedSsids;
        uint32_t probeCount = 0;
        int8_t lastRssi = 0;
        uint32_t firstSeenMs = 0;
        uint32_t lastSeenMs = 0;
    };

    void begin(QueueHandle_t outQueue);

    void start();  // no-op if already running
    void stop();
    bool isRunning() const { return _running; }

    uint8_t currentChannel() const { return _currentChannel; }

    size_t apCount() const;
    bool getAp(size_t index, ApBeacon& out) const;  // most-recently-seen first

    size_t clientCount() const;
    bool getClient(size_t index, ProbeClient& out) const;  // most-recently-seen first

private:
    static constexpr size_t kMaxAps = 80;
    static constexpr size_t kMaxClients = 60;
    static constexpr uint8_t kMaxProbedSsidsPerClient = 8;
    static constexpr uint8_t kMaxChannel = 13;
    static constexpr uint32_t kChannelDwellMs = 300;  // per-channel listen time while hopping

    static void taskEntry(void* arg);
    void run();
    static void promiscuousRxTrampoline(void* buf, wifi_promiscuous_pkt_type_t type);
    void onManagementFrame(const uint8_t* p, uint16_t len, int8_t rssi);
    void handleApFrame(const uint8_t bssid[6], uint16_t bodyStart, const uint8_t* p, uint16_t len, bool privacy,
                        int8_t rssi);
    void handleProbeRequest(const uint8_t clientMac[6], uint16_t bodyStart, const uint8_t* p, uint16_t len,
                             int8_t rssi);
    void updateAp(const uint8_t bssid[6], const String& ssid, bool hasSsid, uint8_t channel, wifi_auth_mode_t enc,
                  int8_t rssi, bool wpsEnabled, bool wpsLocked, uint16_t wpsConfigMethods);
    void updateClient(const uint8_t mac[6], const String& probedSsid, bool hasSsid, int8_t rssi);
    void notify(const String& text);

    mutable SemaphoreHandle_t _mutex = nullptr;
    std::vector<ApBeacon> _aps;
    std::vector<ProbeClient> _clients;
    QueueHandle_t _outQueue = nullptr;

    std::atomic<bool> _running{false};
    std::atomic<uint8_t> _currentChannel{1};
};

extern BeaconProbeSniffer g_beaconProbeSniffer;
