#include "OtaScreen.h"
#include "../UiManager.h"
#include "../Theme.h"
#include "../Chrome.h"
#include "../../net/OtaUpdater.h"

OtaScreen& OtaScreen::instance() {
    static OtaScreen s;
    return s;
}

void OtaScreen::onEnter() {
    _state = State::UrlEntry;
    _url = "";
    _errorMsg = "";
    _updatingFrames = 0;
    g_ui.setTextEntryMode(true);
}

void OtaScreen::onExit() {
    g_ui.setTextEntryMode(false);  // redundant with UiManager's own safety net, but explicit costs nothing
}

void OtaScreen::onKey(UiKey key, char ch) {
    switch (_state) {
        case State::UrlEntry:
            if (key == UiKey::Char) {
                if (_url.length() < kMaxUrlLen) _url += ch;
            } else if (key == UiKey::Back) {
                if (_url.length() > 0) {
                    _url.remove(_url.length() - 1);
                } else {
                    g_ui.popScreen();
                }
            } else if (key == UiKey::Enter) {
                if (_url.length() > 7) {  // longer than just "http://"
                    g_ui.setTextEntryMode(false);
                    _updatingFrames = 0;
                    _state = State::Updating;
                }
            }
            break;

        case State::Updating:
            // No input accepted mid-update - see the header for why
            // this deliberately blocks the whole UI task anyway.
            break;

        case State::Result:
            if (key == UiKey::Enter || key == UiKey::Back) {
                _errorMsg = "";
                g_ui.setTextEntryMode(true);
                _state = State::UrlEntry;
            }
            break;
    }
}

void OtaScreen::update(uint32_t /*nowMs*/) {
    if (_state != State::Updating) return;

    _updatingFrames++;
    // Waits for the "downloading..." frame from the FIRST time we
    // entered this state to have actually reached the physical display
    // (one full update()+draw()+pushSprite() cycle) before starting the
    // blocking call below - otherwise the screen would freeze on
    // whatever was on it before Enter was pressed, with no visible
    // indication anything is happening.
    if (_updatingFrames != 2) return;

    String err;
    bool ok = OtaUpdater::run(_url, err);  // blocks; reboots the device on success and never returns here
    if (!ok) {
        _errorMsg = err;
        _state = State::Result;
    }
}

void OtaScreen::draw(M5Canvas& gfx) {
    gfx.fillScreen(theme::BG);
    chrome::drawHeader(gfx, "OTA UPDATE");

    switch (_state) {
        case State::UrlEntry: {
            gfx.setTextColor(theme::CYAN, theme::BG);
            gfx.setCursor(6, 22);
            gfx.print("firmware.bin URL (http only):");

            gfx.fillRect(6, 34, gfx.width() - 12, 10, theme::PANEL_BG);
            String shown = _url;
            constexpr int kMaxVisible = 36;
            if (shown.length() > kMaxVisible) shown = shown.substring(shown.length() - kMaxVisible);
            gfx.setTextColor(theme::GREEN_BRIGHT, theme::PANEL_BG);
            gfx.setCursor(8, 35);
            gfx.print(shown);

            gfx.setTextColor(theme::GREY, theme::BG);
            gfx.setCursor(6, 50);
            gfx.print("e.g. http://192.168.1.50:8000/firmware.bin");
            gfx.setCursor(6, 60);
            gfx.print("served from a machine on this LAN, e.g.");
            gfx.setCursor(6, 70);
            gfx.print("python3 -m http.server in .pio/build/.../");

            gfx.setTextColor(theme::AMBER, theme::BG);
            gfx.setCursor(6, 86);
            gfx.print("a bad image just fails validation and");
            gfx.setCursor(6, 96);
            gfx.print("reboots into the current firmware again.");

            gfx.setTextColor(theme::GREY, theme::BG);
            gfx.setCursor(4, gfx.height() - 9);
            gfx.print("ENTER:start  DEL:erase/back");
            break;
        }

        case State::Updating:
            gfx.setTextColor(theme::CYAN, theme::BG);
            gfx.setCursor(6, 30);
            gfx.print("downloading + flashing...");
            gfx.setTextColor(theme::RED, theme::BG);
            gfx.setCursor(6, 44);
            gfx.print("DO NOT power off or reset");
            gfx.setTextColor(theme::GREY, theme::BG);
            gfx.setCursor(6, 58);
            gfx.print("device reboots on its own when done");
            break;

        case State::Result:
            gfx.setTextColor(theme::RED, theme::BG);
            gfx.setCursor(6, 24);
            gfx.print("update failed:");
            gfx.setTextColor(theme::GREY, theme::BG);
            gfx.setCursor(6, 36);
            {
                String msg = _errorMsg;
                if (msg.length() > 38) msg = msg.substring(0, 38);
                gfx.print(msg);
            }
            gfx.setTextColor(theme::GREEN, theme::BG);
            gfx.setCursor(6, 50);
            gfx.print("current firmware is untouched and still");
            gfx.setCursor(6, 60);
            gfx.print("what's running - nothing was left half-flashed");

            gfx.setTextColor(theme::MAGENTA, theme::BG);
            gfx.setCursor(4, gfx.height() - 9);
            gfx.print("ENTER/DEL: try again");
            break;
    }
}
