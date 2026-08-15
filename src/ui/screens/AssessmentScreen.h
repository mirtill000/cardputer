#pragma once

#include "Screen.h"

// "AUTO ASSESS": one-button workflow — drives AssessmentRunner through
// discovery -> per-host port scan -> HTML report, showing the current
// phase and progress. See scan/AssessmentRunner.h (credential auditing
// is intentionally left out of the chain).
class AssessmentScreen : public Screen {
public:
    static AssessmentScreen& instance();

    void onEnter() override;
    void onKey(UiKey key, char ch) override;
    void onScanEvent(const ScanNotification& ev) override;
    void draw(M5Canvas& gfx) override;

    const char* helpText() const override {
        return "AUTO ASSESS\n\nOne button: discovery -> port\nscan -> HTML report. Credential\naudit is NOT included.\nENTER: start/stop\nDEL: back (keeps running)";
    }

private:
    static constexpr uint8_t kLogLines = 5;

    void pushLog(const String& line);

    String _log[kLogLines];
    uint8_t _logCount = 0;
};
