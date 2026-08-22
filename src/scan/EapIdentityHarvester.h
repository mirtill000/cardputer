#pragma once

#include <Arduino.h>
#include <atomic>
#include <vector>
#include "../core/EventQueue.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <esp_wifi.h>

// "EAP IDENTITY": passive WPA-Enterprise (802.1X) outer-identity
// harvester. Listens promiscuously for EAP-Response/Identity frames
// (the very first step of an 802.1X association, sent in the CLEAR
// before the PEAP/TTLS TLS tunnel is up) and records the cleartext
// username each supplicant discloses. See net/EapolWire.h's
// parseEapIdentity() for exactly what's read (only the outer identity
// string + the client MAC that sent it — never anything inside the
// later encrypted tunnel) and why this is the same passive-recon
// category as its own PMKID-presence check or BeaconProbeSniffer's
// probe-request SSIDs.
//
// NEVER TRANSMITS ANYTHING — strictly receive-only, same as
// BeaconProbeSniffer/PassiveHostDiscovery/OsFingerprint. A network
// disclosing real usernames in the clear before the tunnel is a
// classic, reportable enterprise-WiFi audit finding on its own; this
// only ever reports it, never acts on it.
//
// CHANNEL LIMITATION, not a bug (read before trusting an empty
// result): like the other on-channel passive listeners
// (PassiveHostDiscovery/OsFingerprint/CdpLldpSniffer, and UNLIKE
// BeaconProbeSniffer which hops), this stays on whatever channel the
// STA is currently associated to, so it only sees 802.1X associations
// happening on THAT channel. To actually harvest identities you either
// need this device parked on the target enterprise AP's channel, or an
// enterprise client associating on the same channel this device is on.
// Not made a channel-hopper on purpose: hopping would drop this
// device's own STA connection (see BeaconProbeSniffer.h) and 802.1X
// auth is a brief, infrequent event you'd likely hop straight past
// anyway — parking on the right channel is the honest approach.
//
// Shares the single esp_wifi promiscuous callback with every other
// promiscuous consumer (see ArpSpoofManager.h's RISK block) — running
// it alongside another promiscuous feature silently starves whichever
// started second; see ui/ActivityStatus.h's on-screen indicator.
class EapIdentityHarvester {
public:
    struct Sighting {
        uint8_t mac[6] = {0};
        String identity;   // the cleartext outer identity, e.g. "user@corp.example"
        uint32_t count = 0;
        uint32_t lastSeenMs = 0;
    };

    void begin(QueueHandle_t outQueue);

    void start();  // no-op if already running
    void stop();
    bool isRunning() const { return _running; }

    size_t count() const;
    bool get(size_t index, Sighting& out) const;  // most-recently-seen first

private:
    static constexpr size_t kMaxSightings = 40;

    static void taskEntry(void* arg);
    void run();
    static void promiscuousRxTrampoline(void* buf, wifi_promiscuous_pkt_type_t type);
    void onPromiscuousFrame(const uint8_t* payload, uint16_t len);
    void observe(const uint8_t mac[6], const String& identity);
    void notify(const String& text);

    mutable SemaphoreHandle_t _mutex = nullptr;
    std::vector<Sighting> _sightings;
    QueueHandle_t _outQueue = nullptr;
    std::atomic<bool> _running{false};
};

extern EapIdentityHarvester g_eapIdentityHarvester;
