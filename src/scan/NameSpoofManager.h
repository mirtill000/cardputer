#pragma once

#include <Arduino.h>
#include <atomic>
#include <vector>
#include "../core/EventQueue.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

// "NAME SPOOF": LAN name-resolution poisoning, the same technique tools
// like Responder use. Listens for two legacy Windows name-resolution
// fallbacks - LLMNR (multicast UDP 224.0.0.252:5355, used when DNS
// doesn't answer) and NBT-NS (broadcast UDP/137, used when LLMNR
// doesn't either) - and answers every query it sees claiming this
// device's own IP owns the name. Plain unicast/broadcast/multicast UDP
// sockets only, no promiscuous mode - unlike ArpSpoofManager/
// DeauthManager/PmkidManager this needs no raw-802.11 capture, so it
// can run alongside a normal WiFi STA connection without dropping it.
//
// RISK / SCOPE: this proves - and logs - that a host on the network
// accepted a forged name-resolution answer from a device it never
// asked to be authoritative for anything. That's a real, reportable
// pentest finding on its own. What this deliberately does NOT do: run
// a fake SMB/HTTP server behind the poisoned name to actually harvest
// an NTLMv2 handshake (the way Responder's downstream capture does) -
// building a correct-enough SMB2 NEGOTIATE/SESSION_SETUP responder is
// a large, separate piece of work and out of scope for this pass. See
// README's "Limiti noti" for the full list of cuts.
class NameSpoofManager {
public:
    struct LogEntry {
        String text;
        uint32_t atMs = 0;
    };

    void begin(QueueHandle_t outQueue);

    // Starts poisoning for at most `durationS` seconds (hard-capped at
    // kMaxDurationS regardless of what's asked for). No-op if already
    // running.
    bool start(uint16_t durationS);
    void stop();
    bool isRunning() const { return _running; }

    uint32_t secondsRemaining() const;
    uint32_t poisonedCount() const { return _poisoned; }

    size_t logCount() const;
    bool getLogEntry(size_t index, LogEntry& out) const;  // most-recent-first

    // Shorter cap than ArpSpoofManager's 600s: this answers EVERY name
    // query it sees (no per-target scoping is possible, unlike ARP
    // spoofing a single chosen host), so a shorter default blast radius
    // in time is the one knob available to keep a session bounded.
    static constexpr uint16_t kMaxDurationS = 300;

private:
    static constexpr size_t kMaxLogEntries = 100;

    static void taskEntry(void* arg);
    void run();
    void log(const String& text);
    void notify(const String& text);
    void notify(ScanEventType type, uint8_t pct = 0);

    mutable SemaphoreHandle_t _mutex = nullptr;
    std::vector<LogEntry> _log;
    QueueHandle_t _outQueue = nullptr;

    std::atomic<bool> _running{false};
    uint32_t _startMs = 0;
    uint32_t _durationMs = 0;
    std::atomic<uint32_t> _poisoned{0};
};

extern NameSpoofManager g_nameSpoofManager;
