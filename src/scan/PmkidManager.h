#pragma once

#include <Arduino.h>
#include <atomic>
#include "../core/EventQueue.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
// For wifi_promiscuous_pkt_type_t - promiscuousRxTrampoline()'s
// signature must match esp_wifi_set_promiscuous_rx_cb()'s expected
// type exactly (real build lesson from ArpSpoofManager/DeauthManager -
// see their headers).
#include <esp_wifi.h>

// WPA handshake capture WITHOUT deauthenticating anyone — the less
// invasive sibling of DeauthManager. Many WPA2-PSK access points
// (anything supporting fast BSS transition / PMKSA caching, which is
// most consumer routers today) include the PMKID in the very first
// EAPOL message they send to ANY station that associates, before that
// station has proven it knows the real passphrase. So: associate to
// the target AP with a deliberately-wrong password (never the real
// one — this device doesn't try to guess it), capture whatever EAPOL
// traffic crosses the air during that attempt, and let the same
// offline tools DeauthManager already points at (hashcat mode 22000,
// aircrack-ng) pull the PMKID out of the resulting .pcap.
//
// Since Fase 36, each captured frame is also run through
// net/EapolWire.h's structural classifier — enough to say "this capture
// looks like it has a usable PMKID" right on the device, without
// waiting to find out on a PC. Read that file's own header comment for
// the precise, deliberate line this still never crosses: it reads
// header/flag bits and confirms a PMKID marker's PRESENCE, but never
// the nonce/MIC/PMKID bytes themselves, and never attempts to derive,
// guess, or verify a passphrase from anything captured — this firmware
// still never cracks anything itself, same as DeauthManager.
//
// Whether a given AP actually sends the PMKID unsolicited like this is
// AP-firmware-dependent — some do, some don't, and this device has no
// way to know in advance. A pcap with no usable PMKID in it isn't a
// bug, it's just the AP being one that doesn't offer it.
class PmkidManager {
public:
    void begin(QueueHandle_t outQueue);

    // ssid/bssid/channel: from a WAR DRIVING sighting. No-op if already
    // running.
    bool start(const String& ssid, const String& bssid, uint8_t channel);
    bool isRunning() const { return _running; }

    uint32_t capturedPackets() const { return _captured; }
    String pcapPath() const { return _pcapPath; }

    // Structural EAPOL read-out for the capture just finished (or in
    // progress) - see net/EapolWire.h. pmkidLikelyCaptured() is the
    // headline verdict: a Message 1 carrying a PMKID KDE was seen.
    bool pmkidLikelyCaptured() const { return _pmkidSeen; }
    uint32_t message1Count() const { return _m1Count; }
    uint32_t message2Count() const { return _m2Count; }

private:
    static constexpr uint32_t kCaptureWindowMs = 8000;  // bounded - the association attempt fails on its own well before this
    static constexpr uint8_t kCaptureQueueDepth = 16;

    struct CapturedFrame {
        uint8_t data[256];
        uint16_t capturedLen = 0;
        uint16_t originalLen = 0;
    };

    static void taskEntry(void* arg);
    void run();
    static void promiscuousRxTrampoline(void* buf, wifi_promiscuous_pkt_type_t type);
    void onCapturedFrame(const uint8_t* payload, uint16_t len);
    void notify(const String& text);

    QueueHandle_t _outQueue = nullptr;
    QueueHandle_t _captureQueue = nullptr;

    std::atomic<bool> _running{false};
    String _ssid;
    uint8_t _apBssid[6] = {0};
    uint8_t _selfMac[6] = {0};
    uint8_t _channel = 1;
    std::atomic<uint32_t> _captured{0};
    std::atomic<uint32_t> _m1Count{0};
    std::atomic<uint32_t> _m2Count{0};
    std::atomic<bool> _pmkidSeen{false};
    String _pcapPath;
};

extern PmkidManager g_pmkidManager;
