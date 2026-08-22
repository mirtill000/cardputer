#pragma once

#include <Arduino.h>
#include <IPAddress.h>
#include <atomic>
#include "../core/EventQueue.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

// SMB posture probe against a host with an open SMB/NetBIOS port (445 or
// 139). Two read-only Negotiate exchanges, each on its own short-lived
// connection:
//   1. an SMB1 SMB_COM_NEGOTIATE - the same first message any legacy
//      Windows/Samba client sends. A well-formed reply means the server
//      still speaks SMBv1 at all (EternalBlue-class attack surface, a
//      finding in itself) and carries the Security Mode flags (user- vs
//      share-level, plaintext vs challenge/response, signing).
//   2. an SMB2 NEGOTIATE offering dialects 2.0.2 / 2.1 / 3.0 / 3.0.2,
//      reporting the modern dialect the server picks and whether SMB2
//      message signing is REQUIRED.
// From those it derives an overall posture verdict (Weak / Fair / Ok).
//
// SCOPE (deliberately small, unchanged): this is NOT share/user
// enumeration. No SMB Session Setup, no null-credential login, no Tree
// Connect, no NetShareEnum/DCE-RPC - all real protocol work judged out
// of scope (comparable-risk to the SSH credential guessing we declined).
// Everything reported is what the server volunteers in its two Negotiate
// responses - the SMB equivalent of a banner grab, no more intrusive.
class SmbNegotiateCheck {
public:
    // Overall verdict derived from both negotiate exchanges (see run()).
    enum class Posture : uint8_t { Unknown, Ok, Fair, Weak };

    struct Result {
        bool done = false;         // a probe has completed (success or failure)
        bool connected = false;    // TCP connect to the SMB port succeeded
        bool negotiated = false;   // a well-formed SMB1 negotiate response came back
        bool userLevelSecurity = false;  // SMB1 SecurityMode bit0: user-level (set) vs share-level (clear)
        bool challengeResponse = false;   // bit1: encrypted passwords (clear = plaintext accepted)
        bool signingEnabled = false;      // bit2 (SMB1)
        bool signingRequired = false;     // bit3 (SMB1)
        int16_t dialectIndex = -1;        // SMB1 server's chosen dialect index, or -1 if none

        // SMBv1 exposure: true iff the SMB1 negotiate above succeeded -
        // i.e. the server still answers SMBv1 at all. Mirrors `negotiated`,
        // named for clarity at call sites and in the posture logic.
        bool smb1Enabled = false;

        // SMB2 negotiate results (second connection).
        bool smb2Supported = false;
        int16_t smb2Dialect = -1;          // raw DialectRevision, e.g. 0x0311 (-1 if none)
        bool smb2SigningEnabled = false;   // SMB2 SecurityMode bit0
        bool smb2SigningRequired = false;  // SMB2 SecurityMode bit1

        Posture posture = Posture::Unknown;
        String postureNote;  // short "posture: WEAK (reasons)" line

        String note;         // human-readable summary line (mirrors postureNote once done)
    };

    void begin(QueueHandle_t outQueue);

    bool start(const IPAddress& ip, uint16_t port);  // false if a probe is already running
    bool isRunning() const { return _running; }

    Result result() const;  // mutex-protected copy of the latest result

    // Human name for a raw SMB2 DialectRevision (e.g. 0x0311 -> "3.1.1").
    static String dialectName(int16_t smb2Dialect);

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
