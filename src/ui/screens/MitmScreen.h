#pragma once

#include "Screen.h"
#include <IPAddress.h>

// "MITM AUDIT": drives ArpSpoofManager against one explicit target host
// (set via setTarget() before this screen is pushed, from
// HostDetailScreen). Idle lets you tune the session duration, toggle
// promiscuous traffic sniffing, and manage the small DNS-spoof
// hostname->IP list; Running shows a persistent "MITM ACTIVE" header
// (never hidden — see ArpSpoofManager.h on why this shouldn't be a
// quiet background feature the way WAR DRIVING is) plus a live log.
class MitmScreen : public Screen {
public:
    static MitmScreen& instance();

    void setTarget(const IPAddress& ip) { _target = ip; }

    void onEnter() override;
    void onExit() override;
    void onKey(UiKey key, char ch) override;
    void onScanEvent(const ScanNotification& ev) override;
    void draw(M5Canvas& gfx) override;

private:
    enum class State { Idle, Running, DnsAddHost, DnsAddIp };

    static constexpr uint8_t kLogLines = 5;

    void pushLog(const String& line);

    State _state = State::Idle;
    IPAddress _target;
    uint16_t _durationS = 120;
    bool _sniffTraffic = true;
    String _log[kLogLines];
    uint8_t _logCount = 0;
    String _pendingHost;
    String _pendingIpText;
};
