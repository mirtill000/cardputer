#include "SettingsScreen.h"
#include "OtaScreen.h"
#include "../UiManager.h"
#include "../Theme.h"
#include "../Chrome.h"
#include "../../core/Config.h"

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
        default:
            break;
    }
}

void SettingsScreen::onKey(UiKey key, char /*ch*/) {
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
        "TIMEOUT (ms)", "THREADS", "PROBE DELAY (ms)", "PORT START", "PORT END", "AUTO-EXPORT",
    };
    String values[kFieldCount] = {
        String(g_config.scanTimeoutMs),   String(g_config.maxConcurrentProbes),
        String(g_config.interProbeDelayMs), String(g_config.portRangeStart),
        String(g_config.portRangeEnd),    g_config.autoExportOnScanFinish ? String("ON") : String("OFF"),
    };

    constexpr int16_t kRowH = 14;
    constexpr int16_t kTop = 20;

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

    gfx.setTextColor(theme::GREY, theme::BG);
    gfx.setCursor(4, gfx.height() - 9);
    gfx.print("</>:adjust ENTER/DEL:exit O:update");
}
