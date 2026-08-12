#pragma once

#include <Arduino.h>
#include <atomic>
#include "../core/EventQueue.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

// Captive-portal DETECTION — the same benign check every operating system
// runs after joining a network: fetch a well-known URL that is supposed
// to return "204 No Content" and see whether the reply was intercepted.
//   - a real 204 with no body  -> open internet, no portal
//   - a redirect or an HTML 200 -> a captive portal is intercepting HTTP
// It reports the portal's presence (and the redirect URL, when the portal
// hands one back) so the user knows to open a browser and sign in.
//
// This DETECTS a portal; it does not attempt to bypass, evade, or defeat
// one. Circumventing a captive portal's access control is unauthorized
// access to a third-party network and is out of scope for this firmware
// by design — see README "Limiti noti".
class CaptivePortalDetector {
public:
    enum class Status : uint8_t {
        Idle,
        Checking,
        OpenInternet,     // 204 received: no portal in the way
        PortalDetected,   // interception seen (redirect / unexpected body)
        NoConnectivity,   // couldn't reach the check endpoint at all
    };

    struct Result {
        Status status = Status::Idle;
        int httpStatus = 0;   // status code seen on the check request, if any
        String portalUrl;      // redirect Location the portal returned, if any
    };

    void begin(QueueHandle_t outQueue);

    bool start();  // no-op if already running or WiFi isn't connected
    bool isRunning() const { return _running; }

    Result result() const;

private:
    static constexpr uint16_t kTimeoutMs = 4000;

    static void taskEntry(void* arg);
    void run();
    void notify(const String& text);

    mutable SemaphoreHandle_t _mutex = nullptr;
    QueueHandle_t _outQueue = nullptr;
    std::atomic<bool> _running{false};
    Result _result;
};

extern CaptivePortalDetector g_captivePortalDetector;
