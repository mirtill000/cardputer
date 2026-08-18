#pragma once

#include "Screen.h"

// "NAME SPOOF": drives NameSpoofManager (LLMNR/NBT-NS poisoning). Not
// tied to a single target the way MITM AUDIT is - it answers whichever
// host on the LAN happens to ask, so there's no per-host setup step,
// just a duration and a start/stop toggle. See scan/NameSpoofManager.h
// for what this does and doesn't do.
class NameSpoofScreen : public Screen {
public:
    static NameSpoofScreen& instance();

    void onEnter() override;
    void onKey(UiKey key, char ch) override;
    void onScanEvent(const ScanNotification& ev) override;
    void draw(M5Canvas& gfx) override;

    const char* title() const override { return "NSPF"; }
    const char* helpText() const override {
        return "NAME SPOOF\nMENU>Ent(NSPF)\nAnswers every LLMNR/NBT-NS\nname query on the LAN, claiming\nthis device's IP. </>: duration\nENTER: start/stop\nDEL: back (stops session)";
    }

private:
    enum class State { Idle, Running };

    static constexpr uint8_t kLogLines = 5;

    void pushLog(const String& line);

    State _state = State::Idle;
    uint16_t _durationS = 120;
    String _log[kLogLines];
    uint8_t _logCount = 0;
};
