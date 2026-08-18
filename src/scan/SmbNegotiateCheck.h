#pragma once

#include <Arduino.h>
#include <IPAddress.h>
#include <atomic>
#include "../core/EventQueue.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

// Minimal SMB1 "Negotiate Protocol" probe against a host with an open
// SMB/NetBIOS port (445 or 139). It sends exactly one SMB_COM_NEGOTIATE
// request — the same first message any Windows/Samba client sends on
// connect — and reads back the server's advertised Security Mode flags.
//
// SCOPE (deliberately small): this is NOT share/user enumeration. It
// does no SMB Session Setup, no null-credential login, no Tree Connect,
// no NetShareEnum/DCE-RPC — all of which are real protocol work and were
// judged out of scope (comparable-risk to the SSH credential guessing we
// declined). All this reports is what the server volunteers in its
// Negotiate response: whether it uses user-level vs the ancient
// share-level security model, whether it still accepts plaintext
// passwords (no challenge/response), and whether SMB signing is enabled/
// required. Those flags are a misconfiguration/legacy-exposure signal —
// the SMB equivalent of a banner grab, no more intrusive than that.
class SmbNegotiateCheck {
public:
    struct Result {
        bool done = false;         // a probe has completed (success or failure)
        bool connected = false;    // TCP connect to the SMB port succeeded
        bool negotiated = false;   // a well-formed SMB negotiate response came back
        bool userLevelSecurity = false;  // SecurityMode bit0: user-level (set) vs share-level (clear)
        bool challengeResponse = false;   // bit1: encrypted passwords (clear = plaintext accepted)
        bool signingEnabled = false;      // bit2
        bool signingRequired = false;     // bit3
        int16_t dialectIndex = -1;        // server's chosen dialect, or 0xFFFF/-1 if none
        String note;                       // human-readable summary line
    };

    void begin(QueueHandle_t outQueue);

    bool start(const IPAddress& ip, uint16_t port);  // false if a probe is already running
    bool isRunning() const { return _running; }

    Result result() const;  // mutex-protected copy of the latest result

private:
    static void taskEntry(void* arg);
    void run();
    void notify(const String& text);
    void notify(ScanEventType type, uint8_t pct = 0);

    mutable SemaphoreHandle_t _mutex = nullptr;
    QueueHandle_t _outQueue = nullptr;
    std::atomic<bool> _running{false};

    IPAddress _ip;
    uint16_t _port = 445;
    Result _result;
};

extern SmbNegotiateCheck g_smbCheck;
