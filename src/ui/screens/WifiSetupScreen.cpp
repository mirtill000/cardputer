#include "WifiSetupScreen.h"
#include "../UiManager.h"
#include "../Theme.h"
#include "../Chrome.h"
#include "../../net/TimeSync.h"
#include <algorithm>

namespace {
constexpr uint32_t kConnectTimeoutMs = 15000;
constexpr size_t kMaxPasswordLen = 63;  // WPA2-PSK's actual max
}  // namespace

WifiSetupScreen& WifiSetupScreen::instance() {
    static WifiSetupScreen s;
    return s;
}

void WifiSetupScreen::onEnter() {
    _state = State::Idle;
    _networks.clear();
    _password = "";
}

void WifiSetupScreen::onExit() {
    g_ui.setTextEntryMode(false);  // redundant with UiManager's own safety net, but explicit costs nothing
}

void WifiSetupScreen::startScan() {
    _networks.clear();
    _selected = 0;
    g_wifi.beginScan();
    _state = State::Scanning;
}

void WifiSetupScreen::enterPasswordEntry(const String& ssid) {
    _pendingSsid = ssid;
    _password = "";
    g_ui.setTextEntryMode(true);
    _state = State::PasswordEntry;
}

void WifiSetupScreen::attemptConnect() {
    g_ui.setTextEntryMode(false);
    _savingOnConnect = true;
    _pendingSavedIndex = -1;
    g_wifi.beginConnectWithCredentials(_pendingSsid, _password);
    _connectStartMs = millis();
    _state = State::Connecting;
}

void WifiSetupScreen::connectToSavedNetwork(uint8_t index) {
    _pendingSsid = g_wifi.savedNetworkSsid(index);
    _savingOnConnect = false;
    _pendingSavedIndex = (int16_t)index;
    g_wifi.connectSaved(index);
    _connectStartMs = millis();
    _state = State::Connecting;
}

void WifiSetupScreen::onKey(UiKey key, char ch) {
    switch (_state) {
        case State::Idle:
            if (key == UiKey::Enter) {
                startScan();
            } else if (key == UiKey::Char && (ch == 's' || ch == 'S') && g_wifi.savedNetworkCount() > 0) {
                _selected = 0;
                _state = State::SavedList;
            } else if (key == UiKey::Char && (ch == 'f' || ch == 'F') && g_wifi.hasSavedCredentials()) {
                g_wifi.forgetSavedCredentials();
            } else if (key == UiKey::Back) {
                g_ui.popScreen();
            }
            break;

        case State::SavedList:
            if (key == UiKey::Up) {
                if (_selected > 0) _selected--;
            } else if (key == UiKey::Down) {
                if (_selected + 1 < g_wifi.savedNetworkCount()) _selected++;
            } else if (key == UiKey::Enter) {
                connectToSavedNetwork((uint8_t)_selected);
            } else if (key == UiKey::Char && (ch == 'f' || ch == 'F')) {
                g_wifi.forgetSavedNetwork((uint8_t)_selected);
                if (_selected > 0 && _selected >= g_wifi.savedNetworkCount()) _selected--;
                if (g_wifi.savedNetworkCount() == 0) _state = State::Idle;
            } else if (key == UiKey::Back) {
                _state = State::Idle;
            }
            break;

        case State::Scanning:
            // Can't cancel an in-flight WiFi.scanNetworks() cleanly;
            // Back just stops us from waiting on it.
            if (key == UiKey::Back) _state = State::Idle;
            break;

        case State::NetworkList:
            if (key == UiKey::Up) {
                if (_selected > 0) _selected--;
            } else if (key == UiKey::Down) {
                if (_selected + 1 < _networks.size()) _selected++;
            } else if (key == UiKey::Enter) {
                if (!_networks.empty()) {
                    const auto& n = _networks[_selected];
                    if (n.encryption == WIFI_AUTH_OPEN) {
                        _pendingSsid = n.ssid;
                        _password = "";
                        attemptConnect();
                    } else {
                        enterPasswordEntry(n.ssid);
                    }
                }
            } else if (key == UiKey::Char && (ch == 'r' || ch == 'R')) {
                startScan();
            } else if (key == UiKey::Back) {
                _state = State::Idle;
            }
            break;

        case State::PasswordEntry:
            if (key == UiKey::Char) {
                if (_password.length() < kMaxPasswordLen) _password += ch;
            } else if (key == UiKey::Back) {
                if (_password.length() > 0) {
                    _password.remove(_password.length() - 1);
                } else {
                    g_ui.setTextEntryMode(false);
                    _state = State::NetworkList;
                }
            } else if (key == UiKey::Enter) {
                attemptConnect();
            }
            break;

        case State::Connecting:
            // Ignore input — WiFi.begin() can't be cleanly aborted
            // mid-attempt, so there's nothing useful Back could do here.
            break;

        case State::Result:
            if (key == UiKey::Enter || key == UiKey::Back) {
                _state = _lastConnectOk ? State::Idle : State::NetworkList;
                if (_lastConnectOk) g_ui.popScreen();
            }
            break;
    }
}

void WifiSetupScreen::update(uint32_t nowMs) {
    if (_state == State::Scanning) {
        int16_t count = g_wifi.scanStatus();
        if (count >= 0) {
            std::vector<WifiManager::ScanResult> raw;
            WifiManager::ScanResult r;
            for (int16_t i = 0; i < count; i++) {
                if (g_wifi.getScanResult(i, r) && r.ssid.length()) raw.push_back(r);
            }

            _networks.clear();
            for (auto& cand : raw) {
                bool merged = false;
                for (auto& existing : _networks) {
                    if (existing.ssid == cand.ssid) {
                        if (cand.rssi > existing.rssi) existing = cand;
                        merged = true;
                        break;
                    }
                }
                if (!merged) _networks.push_back(cand);
            }
            std::sort(_networks.begin(), _networks.end(),
                      [](const WifiManager::ScanResult& a, const WifiManager::ScanResult& b) { return a.rssi > b.rssi; });

            _state = State::NetworkList;
        } else if (count == WifiManager::kScanFailed) {
            _state = State::Idle;
        }
        // kScanRunning: keep waiting.
    } else if (_state == State::Connecting) {
        if (g_wifi.isConnected()) {
            if (_savingOnConnect) {
                g_wifi.saveCredentials(_pendingSsid, _password);
            } else if (_pendingSavedIndex >= 0) {
                g_wifi.touchSavedNetwork((uint8_t)_pendingSavedIndex);
            }
            TimeSync::begin();  // re-arm now that a connection exists - see main.cpp
            _lastConnectOk = true;
            _state = State::Result;
        } else if (g_wifi.connectFailed() || (nowMs - _connectStartMs > kConnectTimeoutMs)) {
            _lastConnectOk = false;
            _state = State::Result;
        }
    }
}

void WifiSetupScreen::draw(M5Canvas& gfx) {
    gfx.fillScreen(theme::BG);
    chrome::drawHeader(gfx, "WIFI SETUP");

    switch (_state) {
        case State::Idle: {
            gfx.setTextColor(theme::GREEN, theme::BG);
            gfx.setCursor(6, 22);
            if (g_wifi.isConnected()) {
                gfx.print("connected: ");
                gfx.print(g_wifi.currentSsid());
                gfx.setCursor(6, 32);
                gfx.print("ip: ");
                gfx.print(g_wifi.localIP().toString());
            } else if (g_wifi.hasSavedCredentials()) {
                gfx.print("saved: ");
                gfx.print(g_wifi.savedSsid());
                gfx.setCursor(6, 32);
                gfx.setTextColor(theme::AMBER, theme::BG);
                gfx.print("not connected");
            } else {
                gfx.print("no network configured");
            }

            gfx.setTextColor(theme::MAGENTA, theme::BG);
            gfx.setCursor(6, 50);
            gfx.print("ENTER: scan for networks");

            gfx.setTextColor(theme::GREY, theme::BG);
            gfx.setCursor(4, gfx.height() - 9);
            if (g_wifi.savedNetworkCount() > 0) {
                gfx.print("ENTER:scan S:saved F:forget DEL:back");
            } else {
                gfx.print("DEL:back");
            }
            break;
        }

        case State::Scanning:
            gfx.setTextColor(theme::CYAN, theme::BG);
            gfx.setCursor(6, 30);
            gfx.print("scanning...");
            break;

        case State::NetworkList:
            drawNetworkList(gfx, 20);
            gfx.setTextColor(theme::GREY, theme::BG);
            gfx.setCursor(4, gfx.height() - 9);
            gfx.print("ENTER:select R:rescan DEL:back");
            break;

        case State::SavedList:
            drawSavedList(gfx, 20);
            gfx.setTextColor(theme::GREY, theme::BG);
            gfx.setCursor(4, gfx.height() - 9);
            gfx.print("ENTER:connect F:forget DEL:back");
            break;

        case State::PasswordEntry: {
            gfx.setTextColor(theme::GREEN, theme::BG);
            gfx.setCursor(6, 24);
            gfx.print("network: ");
            gfx.print(_pendingSsid);

            gfx.setTextColor(theme::CYAN, theme::BG);
            gfx.setCursor(6, 38);
            gfx.print("password:");

            gfx.fillRect(6, 48, gfx.width() - 12, 10, theme::PANEL_BG);
            String shown = _password;
            constexpr int kMaxVisible = 36;
            if (shown.length() > kMaxVisible) shown = shown.substring(shown.length() - kMaxVisible);
            gfx.setTextColor(theme::GREEN_BRIGHT, theme::PANEL_BG);
            gfx.setCursor(8, 49);
            gfx.print(shown);

            gfx.setTextColor(theme::GREY, theme::BG);
            gfx.setCursor(6, 62);
            gfx.print(_password.length());
            gfx.print(" chars");

            gfx.setCursor(4, gfx.height() - 9);
            gfx.print("ENTER:connect DEL:erase/back");
            break;
        }

        case State::Connecting:
            gfx.setTextColor(theme::CYAN, theme::BG);
            gfx.setCursor(6, 30);
            gfx.print("connecting to ");
            gfx.print(_pendingSsid);
            gfx.print("...");
            break;

        case State::Result:
            if (_lastConnectOk) {
                gfx.setTextColor(theme::GREEN, theme::BG);
                gfx.setCursor(6, 24);
                gfx.print("connected!");
                gfx.setCursor(6, 34);
                gfx.print("ip: ");
                gfx.print(g_wifi.localIP().toString());
                gfx.setTextColor(theme::GREY, theme::BG);
                gfx.setCursor(6, 50);
                gfx.print("saved - reconnects on its own next boot");
            } else {
                gfx.setTextColor(theme::RED, theme::BG);
                gfx.setCursor(6, 24);
                gfx.print("connection failed");
                gfx.setTextColor(theme::GREY, theme::BG);
                gfx.setCursor(6, 34);
                gfx.print("wrong password, or out of range?");
            }
            gfx.setTextColor(theme::MAGENTA, theme::BG);
            gfx.setCursor(4, gfx.height() - 9);
            gfx.print(_lastConnectOk ? "ENTER/DEL: back" : "ENTER/DEL: try again");
            break;
    }
}

void WifiSetupScreen::drawSavedList(M5Canvas& gfx, int16_t top) {
    uint8_t count = g_wifi.savedNetworkCount();
    if (count == 0) {
        gfx.setTextColor(theme::AMBER, theme::BG);
        gfx.setCursor(6, top + 4);
        gfx.print("no saved networks");
        return;
    }

    constexpr int16_t kRowH = 10;
    for (uint8_t i = 0; i < count; i++) {
        int16_t y = top + (int16_t)i * kRowH;
        bool sel = (i == _selected);
        uint16_t rowBg = sel ? theme::GREEN_DIM : theme::BG;
        if (sel) gfx.fillRect(0, y, gfx.width(), kRowH, rowBg);

        gfx.setTextColor(sel ? theme::GREEN_BRIGHT : theme::GREEN, rowBg);
        gfx.setCursor(2, y + 1);
        if (i == 0) gfx.print("* ");  // most-recently-used
        else gfx.print("  ");
        gfx.print(g_wifi.savedNetworkSsid(i));
    }
}

void WifiSetupScreen::drawNetworkList(M5Canvas& gfx, int16_t top) {
    if (_networks.empty()) {
        gfx.setTextColor(theme::AMBER, theme::BG);
        gfx.setCursor(6, top + 4);
        gfx.print("no networks found");
        return;
    }

    constexpr int16_t kRowH = 10;
    constexpr int16_t kMaxRows = 9;  // top(20) + 9*10 = 110, clears the footer hint row

    size_t first = 0;
    if (_selected >= kMaxRows) first = _selected - kMaxRows + 1;

    for (size_t row = 0; row < kMaxRows; row++) {
        size_t i = first + row;
        if (i >= _networks.size()) break;
        const auto& n = _networks[i];

        int16_t y = top + (int16_t)row * kRowH;
        bool sel = (i == _selected);
        uint16_t rowBg = sel ? theme::GREEN_DIM : theme::BG;
        if (sel) gfx.fillRect(0, y, gfx.width(), kRowH, rowBg);

        bool open = (n.encryption == WIFI_AUTH_OPEN);
        // Green = secured (safe default), amber = open — reuses the
        // same "amber means pay attention" convention as the risk
        // colors elsewhere in the app, not a security judgement call.
        uint16_t color = sel ? theme::GREEN_BRIGHT : (open ? theme::AMBER : theme::GREEN);
        gfx.setTextColor(color, rowBg);
        gfx.setCursor(2, y + 1);
        gfx.print(open ? "o " : "* ");

        String ssid = n.ssid;
        if (ssid.length() > 30) ssid = ssid.substring(0, 30);
        gfx.print(ssid);
    }
}
