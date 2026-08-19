#include "SettingsScreen.h"
#include "OtaScreen.h"
#include "FileManagerScreen.h"
#include "CapturesScreen.h"
#include "DiagnosticsScreen.h"
#include "../UiManager.h"
#include "../Theme.h"
#include "../Chrome.h"
#include "../../core/Config.h"
#include "../../scan/PortScanManager.h"
#include "../../storage/ConfigBackup.h"
#include "../../storage/SdCard.h"
#include <SD.h>

namespace {
constexpr const char* kBackupPath = "/config_backup.json";
}  // namespace

SettingsScreen& SettingsScreen::instance() {
    static SettingsScreen s;
    return s;
}

void SettingsScreen::adjust(int direction) {
    switch (_selected) {
        case 0: {  // scan timeout (ms)
            int v = (int)g_config.scanTimeoutMs + direction * 50;
            if (v < 50) v = 50;
            if (v > 5000) v = 5000;
            g_config.scanTimeoutMs = (uint16_t)v;
            break;
        }
        case 1: {  // concurrent probes ("threads")
            int v = (int)g_config.maxConcurrentProbes + direction;
            if (v < 1) v = 1;
            if (v > 8) v = 8;  // matches the hard ceiling in ScanManager/PortScanManager
            g_config.maxConcurrentProbes = (uint8_t)v;
            break;
        }
        case 2: {  // inter-probe delay (ms)
            int v = (int)g_config.interProbeDelayMs + direction * 5;
            if (v < 0) v = 0;
            if (v > 200) v = 200;
            g_config.interProbeDelayMs = (uint16_t)v;
            break;
        }
        case 3: {  // port range start
            int v = (int)g_config.portRangeStart + direction * 10;
            if (v < 1) v = 1;
            if (v > (int)g_config.portRangeEnd) v = g_config.portRangeEnd;  // keep start <= end
            g_config.portRangeStart = (uint16_t)v;
            break;
        }
        case 4: {  // port range end
            int v = (int)g_config.portRangeEnd + direction * 10;
            if (v < (int)g_config.portRangeStart) v = g_config.portRangeStart;
            if (v > 65535) v = 65535;
            g_config.portRangeEnd = (uint16_t)v;
            break;
        }
        case 5:  // auto-export toggle
            g_config.autoExportOnScanFinish = !g_config.autoExportOnScanFinish;
            break;
        case 6:  // sound toggle - gates sound::startBootLoop()/playAlert()/playCredAlert()
            g_config.uiSoundEnabled = g_config.uiSoundEnabled ? 0 : 1;
            break;
        case 7:  // low-power mode toggle - faster backlight dim (see UiManager)
            g_config.lowPowerMode = g_config.lowPowerMode ? 0 : 1;
            break;
        default:
            break;
    }
}

void SettingsScreen::onKey(UiKey key, char ch) {
    // RESTORE is a two-key confirm (see the 'r' handler below): any key
    // other than a second R cancels a pending confirm before doing its
    // own thing, so an armed restore can't fire from an unrelated press.
    if (_restoreArmed && !(key == UiKey::Char && (ch == 'r' || ch == 'R'))) {
        _restoreArmed = false;
        _statusLine = "restore cancelled";
    }

    switch (key) {
        case UiKey::Up:
            _selected = (_selected == 0) ? (uint8_t)(kFieldCount - 1) : (uint8_t)(_selected - 1);
            break;
        case UiKey::Down:
            _selected = (uint8_t)((_selected + 1) % kFieldCount);
            break;
        case UiKey::Left:
            adjust(-1);
            break;
        case UiKey::Right:
            adjust(1);
            break;
        case UiKey::Enter:
        case UiKey::Back:
            g_config.save();
            g_ui.popScreen();
            break;
        case UiKey::Char:
            if (ch == 'o' || ch == 'O') {
                g_config.save();  // same as leaving normally - just also opens OTA UPDATE on top
                g_ui.pushScreen(&OtaScreen::instance());
            } else if (ch == 'b' || ch == 'B') {
                // SD only, never LittleFS - see storage/ConfigBackup.h
                // for why (must survive the same full-chip erase this
                // is meant to help recover from).
                if (!sdcard::isReady()) {
                    _statusLine = "backup needs an SD card";
                } else {
                    g_config.save();  // backs up the RAM copy - flush any pending edit first
                    _statusLine = ConfigBackup::backup(SD, kBackupPath) ? "backed up to SD" : "backup FAILED (see serial log)";
                }
            } else if (ch == 'r' || ch == 'R') {
                // Destructive: overwrites the live WiFi credentials and
                // war-driving allowlist. Arm on the first R, act on the
                // second; the guard at the top of onKey() disarms on any
                // other key.
                if (!sdcard::isReady()) {
                    _statusLine = "restore needs an SD card";
                    _restoreArmed = false;
                } else if (!SD.exists(kBackupPath)) {
                    _statusLine = "no backup found on SD";
                    _restoreArmed = false;
                } else if (!_restoreArmed) {
                    _restoreArmed = true;
                    _statusLine = "press R again to OVERWRITE config from SD";
                } else {
                    _restoreArmed = false;
                    _statusLine = ConfigBackup::restore(SD, kBackupPath) ? "restored from SD" : "restore FAILED (see serial log)";
                }
            } else if (ch == 'f' || ch == 'F') {
                g_config.save();
                g_ui.pushScreen(&FileManagerScreen::instance());
            } else if (ch == 'c' || ch == 'C') {
                g_config.save();
                g_ui.pushScreen(&CapturesScreen::instance());
            } else if (ch == 'd' || ch == 'D') {
                g_config.save();
                g_ui.pushScreen(&DiagnosticsScreen::instance());
            }
            break;
        default:
            break;
    }
}

void SettingsScreen::draw(M5Canvas& gfx) {
    gfx.fillScreen(theme::BG);
    chrome::drawHeader(gfx, "SETTINGS");

    const char* labels[kFieldCount] = {
        "TIMEOUT (ms)", "THREADS",     "PROBE DELAY (ms)", "PORT START",
        "PORT END",     "AUTO-EXPORT", "SOUND",            "LOW-POWER",
    };
    String values[kFieldCount] = {
        String(g_config.scanTimeoutMs),
        String(g_config.maxConcurrentProbes),
        String(g_config.interProbeDelayMs),
        String(g_config.portRangeStart),
        String(g_config.portRangeEnd),
        g_config.autoExportOnScanFinish ? String("ON") : String("OFF"),
        g_config.uiSoundEnabled ? String("ON") : String("OFF"),
        g_config.lowPowerMode ? String("ON") : String("OFF"),
    };

    constexpr int16_t kRowH = 13;  // 8 fields now - a touch tighter to fit
    constexpr int16_t kTop = 18;

    for (uint8_t i = 0; i < kFieldCount; i++) {
        int16_t y = kTop + i * kRowH;
        bool sel = (i == _selected);
        uint16_t rowBg = sel ? theme::PANEL_BG : theme::BG;
        if (sel) gfx.fillRect(0, y, gfx.width(), kRowH - 2, rowBg);

        gfx.setTextColor(sel ? theme::CYAN : theme::GREEN, rowBg);
        gfx.setCursor(4, y + 2);
        gfx.print(labels[i]);

        String valStr = sel ? ("< " + values[i] + " >") : values[i];
        int16_t valX = gfx.width() - (int16_t)valStr.length() * theme::GLYPH_W - 4;
        gfx.setTextColor(sel ? theme::MAGENTA : theme::GREY, rowBg);
        gfx.setCursor(valX, y + 2);
        gfx.print(valStr);
    }

    if (_statusLine.length()) {
        gfx.setTextColor(theme::CYAN, theme::BG);
        gfx.setCursor(4, gfx.height() - 19);
        gfx.print(_statusLine);
    } else if ((uint32_t)g_config.portRangeEnd - (uint32_t)g_config.portRangeStart + 1 >
               PortScanManager::kMaxRangeSpan) {
        // The configured PORT range is wider than a scan will actually
        // probe (PortScanManager::kMaxRangeSpan). Surface the effective
        // cap here instead of capping silently once the scan starts.
        gfx.setTextColor(theme::MAGENTA, theme::BG);
        gfx.setCursor(4, gfx.height() - 19);
        gfx.print(String("range caps at ") + String((unsigned)PortScanManager::kMaxRangeSpan) + " ports");
    }

    gfx.setTextColor(theme::GREY, theme::BG);
    gfx.setCursor(4, gfx.height() - 9);
    gfx.print("</>adj O:ota F:files C:caps D:diag");
}
