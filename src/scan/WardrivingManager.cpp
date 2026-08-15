#include "WardrivingManager.h"
#include "OuiDatabase.h"
#include "PortScanManager.h"
#include "ScanManager.h"
#include "../core/Config.h"
#include "../net/TimeSync.h"
#include "../net/WifiManager.h"
#include "../storage/ResultStore.h"
#include "../storage/SdCard.h"
#include "../storage/NetrunnerPaths.h"
#include "../ui/Sound.h"
#include <Preferences.h>
#include <cstdio>

WardrivingManager g_wardrivingManager;

namespace {
constexpr const char* kAllowlistNamespace = "wardrive_al";

// Parses "aa:bb:cc:dd:ee:ff" (WiFi.BSSIDstr()'s format) into 6 raw
// bytes, for the OUI vendor lookup. Returns false on anything that
// doesn't look like that exact shape.
bool parseMac(const String& s, uint8_t out[6]) {
    if (s.length() != 17) return false;
    for (int i = 0; i < 6; i++) {
        String byteStr = s.substring(i * 3, i * 3 + 2);
        char* endPtr = nullptr;
        long v = strtol(byteStr.c_str(), &endPtr, 16);
        if (endPtr == byteStr.c_str()) return false;
        out[i] = (uint8_t)v;
    }
    return true;
}

const char* encryptionName(wifi_auth_mode_t enc) {
    switch (enc) {
        case WIFI_AUTH_OPEN: return "open";
        case WIFI_AUTH_WEP: return "wep";
        case WIFI_AUTH_WPA_PSK: return "wpa";
        case WIFI_AUTH_WPA2_PSK: return "wpa2";
        case WIFI_AUTH_WPA_WPA2_PSK: return "wpa/wpa2";
        case WIFI_AUTH_WPA2_ENTERPRISE: return "wpa2-ent";
        case WIFI_AUTH_WPA3_PSK: return "wpa3";
        case WIFI_AUTH_WPA2_WPA3_PSK: return "wpa2/wpa3";
        default: return "unknown";
    }
}

// Mutates a local copy (doubling embedded quotes per RFC 4180) and
// wraps in quotes only when actually needed - same approach as
// ResultStore's csvAppendEscaped, duplicated locally rather than
// exported/shared since it's four lines and this is the only other
// place in the codebase that writes CSV by hand.
void csvField(String s, String& out) {
    bool needsQuotes = s.indexOf(',') >= 0 || s.indexOf('"') >= 0 || s.indexOf('\n') >= 0;
    s.replace("\"", "\"\"");
    if (needsQuotes) {
        out += '"';
        out += s;
        out += '"';
    } else {
        out += s;
    }
}

String slotKey(uint8_t index) { return "s" + String(index); }

}  // namespace

void WardrivingManager::begin(QueueHandle_t outQueue) {
    _mutex = xSemaphoreCreateMutex();
    _outQueue = outQueue;
    // Created once, runs forever - internally idles (checking _running)
    // rather than being created/destroyed per start()/stop(), avoiding
    // task-lifecycle races for what's meant to be toggled on and off
    // repeatedly over a session rather than run once to completion like
    // ScanManager/PortScanManager's worker tasks.
    xTaskCreatePinnedToCore(&WardrivingManager::taskEntry, "wardrive", 8192, this, 1, nullptr, 0);
}

void WardrivingManager::start() { _running = true; }
void WardrivingManager::stop() { _running = false; }

void WardrivingManager::taskEntry(void* arg) {
    static_cast<WardrivingManager*>(arg)->run();
}

void WardrivingManager::run() {
    for (;;) {
        if (!_running) {
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }

        runScanCycle();

        // Re-check _running every 500ms while waiting out the interval,
        // so stop() takes effect promptly instead of waiting out a full
        // kScanIntervalMs.
        for (uint32_t waited = 0; waited < kScanIntervalMs && _running; waited += 500) {
            vTaskDelay(pdMS_TO_TICKS(500));
        }
    }
}

void WardrivingManager::runScanCycle() {
    g_wifi.beginScan();

    uint32_t waitStart = millis();
    int16_t count;
    do {
        vTaskDelay(pdMS_TO_TICKS(200));
        count = g_wifi.scanStatus();
    } while (count == WifiManager::kScanRunning && (millis() - waitStart) < 10000);

    if (count < 0) return;  // failed or timed out this cycle - try again next time

    ApSighting toDiscover;
    bool haveToDiscover = false;

    for (int16_t i = 0; i < count; i++) {
        WifiManager::ScanResult r;
        // bssid is the only thing that MUST be present - it's how a
        // sighting is identified/deduped. ssid is allowed to be empty
        // now (see WifiManager::beginScan's show_hidden=true): a
        // network that doesn't broadcast its name used to be silently
        // invisible here entirely, which real war-driving tools don't
        // do - they log it too, keyed by BSSID, just without a name to
        // show. isAllowlisted() below is deliberately never checked
        // against a placeholder name for these - there's no way a user
        // could have knowingly authorized "the specific unnamed network
        // at BSSID X" by typing a name into the allowlist.
        if (!g_wifi.getScanResult(i, r) || r.bssid.isEmpty()) continue;
        bool isHidden = r.ssid.isEmpty();

        bool allowlisted = isHidden ? false : isAllowlisted(r.ssid);
        bool isNew = false;
        ApSighting rec;

        if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
            ApSighting* existing = nullptr;
            for (auto& s : _sightings) {
                if (s.bssid == r.bssid) {
                    existing = &s;
                    break;
                }
            }
            if (existing) {
                existing->rssi = r.rssi;
                existing->lastSeenMs = millis();
                existing->allowlisted = allowlisted;
                rec = *existing;
            } else if (_sightings.size() < kMaxSightings) {
                rec.ssid = isHidden ? String("<hidden>") : r.ssid;
                rec.bssid = r.bssid;
                rec.rssi = r.rssi;
                rec.channel = r.channel;
                rec.encryption = r.encryption;
                rec.open = (r.encryption == WIFI_AUTH_OPEN);
                rec.allowlisted = allowlisted;
                rec.firstSeenMs = millis();
                rec.lastSeenMs = rec.firstSeenMs;

                uint8_t macBytes[6];
                if (parseMac(r.bssid, macBytes)) g_ouiDb.lookup(macBytes, rec.vendor);

                // Evil-twin heuristic: another already-known sighting
                // with the SAME SSID but a DIFFERENT encryption level -
                // flag both, since which one (if either) is the
                // impostor can't be told from this alone (see the class
                // comment / ApSighting::suspicious). Skipped for hidden
                // SSIDs - "<hidden>" isn't a real name to compare.
                if (!isHidden) {
                    for (auto& s : _sightings) {
                        if (s.ssid == rec.ssid && s.encryption != rec.encryption) {
                            s.suspicious = true;
                            s.suspiciousNote = "SSID also seen with different encryption (possible evil twin)";
                            rec.suspicious = true;
                            rec.suspiciousNote = s.suspiciousNote;
                            break;
                        }
                    }
                }

                _sightings.push_back(rec);
                isNew = true;
            }
            xSemaphoreGive(_mutex);
        }

        if (isNew) {
            logSighting(rec);
            if (rec.open) {
                _openCount++;
                sound::playAlert();
            }
            if (rec.suspicious) {
                _suspiciousCount++;
                sound::playCredAlert();  // same urgency tier as a confirmed default credential
                notify("possible evil twin: " + rec.ssid);
            }
            if (rec.open && rec.allowlisted && !rec.discovered && !haveToDiscover) {
                toDiscover = rec;
                haveToDiscover = true;
            }
        }
    }

    if (haveToDiscover) handleOpenAllowlistedAp(toDiscover);
}

void WardrivingManager::logSighting(const ApSighting& ap) {
    fs::FS& fs = sdcard::exportFs();
    // Continuous, ever-growing (append mode, never truncated) sighting
    // log across the device's whole lifetime, not a per-run report -
    // unlike everything netrunner::reportBase() builds, this is one
    // single file, not one per scan/excursion. Still lives under
    // /netrunner (Fase 29 - previously its own /wardrive/ namespace) so
    // every artifact a user would want to pull off the card lands in one
    // shared folder.
    fs.mkdir("/netrunner");

    bool isNewFile = !fs.exists("/netrunner/wardrive.csv");
    File f = fs.open("/netrunner/wardrive.csv", "a");
    if (!f) return;
    if (isNewFile) f.println("time,ssid,bssid,rssi,channel,encryption,vendor,open,allowlisted,suspicious");

    String t = TimeSync::isSynced() ? TimeSync::nowString() : ("uptime:" + String(millis() / 1000));

    String row;
    csvField(t, row);
    row += ',';
    csvField(ap.ssid, row);
    row += ',';
    csvField(ap.bssid, row);
    row += ',';
    row += String(ap.rssi);
    row += ',';
    row += String(ap.channel);
    row += ',';
    csvField(encryptionName(ap.encryption), row);
    row += ',';
    csvField(ap.vendor, row);
    row += ',';
    row += (ap.open ? "1" : "0");
    row += ',';
    row += (ap.allowlisted ? "1" : "0");
    row += ',';
    row += (ap.suspicious ? "1" : "0");

    f.println(row);
    f.close();

    notify("new AP: " + ap.ssid + (ap.open ? " (OPEN)" : ""));
}

void WardrivingManager::handleOpenAllowlistedAp(const ApSighting& ap) {
    notify("connecting to allow-listed AP: " + ap.ssid);

    // Never saved (see WifiManager::saveCredentials) - an open network
    // encountered while war-driving must never displace one of the
    // user's own saved networks in that 3-slot MRU list.
    g_wifi.beginConnectWithCredentials(ap.ssid, "");

    uint32_t connectStart = millis();
    while (!g_wifi.isConnected() && !g_wifi.connectFailed() && (millis() - connectStart) < 15000) {
        vTaskDelay(pdMS_TO_TICKS(300));
    }

    if (!g_wifi.isConnected()) {
        notify("could not join " + ap.ssid);
        g_wifi.autoConnect();  // no-op if nothing is saved
        return;
    }

    g_scanManager.startDiscoveryScan();
    uint32_t scanStart = millis();
    while (g_scanManager.isRunning() && (millis() - scanStart) < 90000) {
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    size_t hostCount = g_scanManager.hostCount();
    size_t aliveCount = 0;
    constexpr size_t kMaxPortScan = 5;  // bounds how long/how invasive one excursion gets
    size_t portScanned = 0;

    HostInfo h;
    for (size_t i = 0; i < hostCount; i++) {
        if (!g_scanManager.getHost(i, h) || !h.alive) continue;
        aliveCount++;
        if (portScanned >= kMaxPortScan) continue;

        g_portScanManager.startScan(h.ip, g_config.portRangeStart, g_config.portRangeEnd);
        uint32_t portStart = millis();
        while (g_portScanManager.isRunning() && (millis() - portStart) < 30000) {
            vTaskDelay(pdMS_TO_TICKS(500));
        }
        portScanned++;
    }

    // Under /netrunner (Fase 29) alongside every other scan report, but
    // labeled explicitly with THIS excursion's ssid+bssid rather than
    // netrunner::reportBase()'s usual "whatever WiFi is currently
    // connected to" - by the time export runs below, this device may
    // already be reconnecting back to its own saved network (see the
    // loop above), so relying on the live SSID here would be fragile.
    // ssid+bssid (not ssid alone) still disambiguates two different APs
    // sharing the same name - the exact evil-twin scenario this module
    // itself flags elsewhere (see ApSighting::suspicious).
    fs::FS& fs = sdcard::exportFs();
    String base = netrunner::reportBase(fs, ap.ssid + "_" + ap.bssid);
    ResultStore::exportJson(fs, (base + ".json").c_str());
    ResultStore::exportCsv(fs, (base + ".csv").c_str());

    notify("discovered " + String((unsigned)aliveCount) + " host(s) on " + ap.ssid);

    _discoveredCount++;
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
        for (auto& s : _sightings) {
            if (s.bssid == ap.bssid) {
                s.discovered = true;
                break;
            }
        }
        xSemaphoreGive(_mutex);
    }

    notify("reconnecting to your own network");
    g_wifi.autoConnect();  // no-op if nothing is saved; otherwise rejoins the MRU-front saved network
}

void WardrivingManager::notify(const String& text) {
    if (!_outQueue) return;
    ScanNotification n;
    n.source = ScanSource::Wardriving;
    n.type = ScanEventType::LogLine;
    n.setText(text.c_str());
    xQueueSend(_outQueue, &n, 0);
}

size_t WardrivingManager::sightingCount() const {
    if (!_mutex || xSemaphoreTake(_mutex, pdMS_TO_TICKS(100)) != pdTRUE) return 0;
    size_t n = _sightings.size();
    xSemaphoreGive(_mutex);
    return n;
}

bool WardrivingManager::getSighting(size_t index, ApSighting& out) const {
    if (!_mutex || xSemaphoreTake(_mutex, pdMS_TO_TICKS(100)) != pdTRUE) return false;
    // Most-recently-seen first for display - _sightings is in discovery
    // order (oldest first), so index from the back.
    bool ok = index < _sightings.size();
    if (ok) out = _sightings[_sightings.size() - 1 - index];
    xSemaphoreGive(_mutex);
    return ok;
}

uint8_t WardrivingManager::allowlistCount() const {
    Preferences prefs;
    if (!prefs.begin(kAllowlistNamespace, /*readOnly=*/true)) return 0;
    uint8_t count = prefs.getUChar("count", 0);
    prefs.end();
    return (count > kMaxAllowlist) ? kMaxAllowlist : count;
}

String WardrivingManager::allowlistSsid(uint8_t index) const {
    if (index >= allowlistCount()) return "";
    Preferences prefs;
    if (!prefs.begin(kAllowlistNamespace, /*readOnly=*/true)) return "";
    String s = prefs.getString(slotKey(index).c_str(), "");
    prefs.end();
    return s;
}

bool WardrivingManager::isAllowlisted(const String& ssid) const {
    uint8_t count = allowlistCount();
    for (uint8_t i = 0; i < count; i++) {
        if (allowlistSsid(i) == ssid) return true;
    }
    return false;
}

bool WardrivingManager::addToAllowlist(const String& ssid) {
    if (ssid.isEmpty() || isAllowlisted(ssid)) return false;
    uint8_t count = allowlistCount();
    if (count >= kMaxAllowlist) return false;

    Preferences prefs;
    if (!prefs.begin(kAllowlistNamespace, /*readOnly=*/false)) return false;
    prefs.putString(slotKey(count).c_str(), ssid);
    prefs.putUChar("count", (uint8_t)(count + 1));
    prefs.end();
    return true;
}

void WardrivingManager::removeFromAllowlist(uint8_t index) {
    uint8_t count = allowlistCount();
    if (index >= count) return;

    Preferences prefs;
    if (!prefs.begin(kAllowlistNamespace, /*readOnly=*/false)) return;

    String entries[kMaxAllowlist];
    for (uint8_t i = 0; i < count; i++) entries[i] = prefs.getString(slotKey(i).c_str(), "");

    uint8_t newCount = 0;
    for (uint8_t i = 0; i < count; i++) {
        if (i == index) continue;
        prefs.putString(slotKey(newCount).c_str(), entries[i]);
        newCount++;
    }
    prefs.remove(slotKey(count - 1).c_str());
    prefs.putUChar("count", newCount);
    prefs.end();
}
