#pragma once

#include "Screen.h"

// Firmware update over HTTP: type a URL to a raw firmware.bin (served
// from a machine on the same LAN, e.g. `python3 -m http.server` in the
// PlatformIO build output directory), confirm, wait.
//
// The download+flash itself (OtaUpdater::run()) is a blocking call, run
// directly on the UI task rather than a background worker - deliberate,
// not an oversight: an OTA update needs a "please don't power off"
// screen the user can't navigate away from anyway, so a frozen display
// showing exactly that for the update's duration is the correct UX, not
// a bug. Reachable from SETTINGS.
class OtaScreen : public Screen {
public:
    static OtaScreen& instance();

    void onEnter() override;
    void onExit() override;
    void onKey(UiKey key, char ch) override;
    void update(uint32_t nowMs) override;
    void draw(M5Canvas& gfx) override;

    const char* helpText() const override {
        return "OTA UPDATE\n\nType a URL to a raw\nfirmware.bin on your LAN,\nENTER to flash.\nDo NOT power off mid-update.\nDEL: erase / back";
    }

private:
    enum class State { UrlEntry, Updating, Result };

    static constexpr size_t kMaxUrlLen = 96;

    State _state = State::UrlEntry;
    String _url;
    String _errorMsg;
    uint8_t _updatingFrames = 0;
};
