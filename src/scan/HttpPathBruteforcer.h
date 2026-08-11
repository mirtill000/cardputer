#pragma once

#include <IPAddress.h>
#include <atomic>
#include <vector>
#include "../core/EventQueue.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

// dirb-style path brute-forcer: given an already-discovered HTTP(S)
// port on a host, tries a small built-in wordlist of common
// sensitive/interesting paths (kPaths in the .cpp) and reports which
// ones return anything other than 404. Plain GET requests, one at a
// time, rate-limited the same as every other probe in this app
// (AppConfig::interProbeDelayMs) — this is enumeration, not an attack
// against anything: nothing here submits data or tries to exploit
// whatever it finds, it only asks "does this path exist".
class HttpPathBruteforcer {
public:
    struct Hit {
        String path;
        uint16_t status = 0;
    };

    void begin(QueueHandle_t outQueue);

    void start(const IPAddress& target, uint16_t port);
    bool isRunning() const { return _running; }

    uint32_t triedCount() const { return _tried; }
    size_t hitCount() const;
    bool getHit(size_t index, Hit& out) const;  // most-recent-first

private:
    static void taskEntry(void* arg);
    void run();
    bool tryPath(const String& path, uint16_t& statusOut);
    void notify(ScanEventType type, uint8_t pct = 0);
    void logHit(const String& path, uint16_t status);

    QueueHandle_t _outQueue = nullptr;
    IPAddress _target;
    uint16_t _port = 80;
    std::atomic<bool> _running{false};
    std::atomic<uint32_t> _tried{0};

    mutable SemaphoreHandle_t _mutex = nullptr;
    std::vector<Hit> _hits;
};

extern HttpPathBruteforcer g_httpBruteforcer;
