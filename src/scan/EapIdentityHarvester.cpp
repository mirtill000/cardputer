#include "EapIdentityHarvester.h"
#include "../net/EapolWire.h"
#include "../storage/SdCard.h"
#include "../core/Types.h"  // macToString
#include <Arduino.h>        // millis()
#include <cstring>

EapIdentityHarvester g_eapIdentityHarvester;

void EapIdentityHarvester::begin(QueueHandle_t outQueue) {
    _mutex = xSemaphoreCreateMutex();
    _outQueue = outQueue;
    // Permanent-task-that-idles pattern, same as PassiveHostDiscovery/
    // OsFingerprint/CdpLldpSniffer - start()/stop() only flip _running.
    xTaskCreatePinnedToCore(&EapIdentityHarvester::taskEntry, "eapident", 4096, this, 1, nullptr, 0);
}

void EapIdentityHarvester::start() { _running = true; }
void EapIdentityHarvester::stop() { _running = false; }

void EapIdentityHarvester::taskEntry(void* arg) {
    static_cast<EapIdentityHarvester*>(arg)->run();
}

void EapIdentityHarvester::run() {
    for (;;) {
        if (!_running) {
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }

        esp_wifi_set_promiscuous_rx_cb(&EapIdentityHarvester::promiscuousRxTrampoline);
        esp_wifi_set_promiscuous(true);
        notify("EAP identity listen on");

        while (_running) vTaskDelay(pdMS_TO_TICKS(500));

        esp_wifi_set_promiscuous(false);
        notify("EAP identity listen off");
    }
}

void EapIdentityHarvester::promiscuousRxTrampoline(void* buf, wifi_promiscuous_pkt_type_t type) {
    if (type != WIFI_PKT_DATA) return;
    auto* pkt = static_cast<wifi_promiscuous_pkt_t*>(buf);
    if (!pkt) return;
    g_eapIdentityHarvester.onPromiscuousFrame(pkt->payload, pkt->rx_ctrl.sig_len);
}

void EapIdentityHarvester::onPromiscuousFrame(const uint8_t* p, uint16_t len) {
    String identity;
    uint8_t mac[6];
    if (!eapol::parseEapIdentity(p, len, identity, mac)) return;
    observe(mac, identity);
}

void EapIdentityHarvester::observe(const uint8_t mac[6], const String& identity) {
    bool isNew = false;
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
        Sighting* existing = nullptr;
        for (auto& s : _sightings) {
            // Dedup on the (MAC, identity) pair - the same client could
            // legitimately present different outer identities across
            // networks, and both are worth keeping.
            if (memcmp(s.mac, mac, 6) == 0 && s.identity == identity) {
                existing = &s;
                break;
            }
        }
        if (existing) {
            existing->count++;
            existing->lastSeenMs = millis();
        } else if (_sightings.size() < kMaxSightings) {
            Sighting s;
            memcpy(s.mac, mac, 6);
            s.identity = identity;
            s.count = 1;
            s.lastSeenMs = millis();
            _sightings.push_back(s);
            isNew = true;
        }
        xSemaphoreGive(_mutex);
    }

    if (isNew) {
        notify("EAP id: " + identity + " (" + macToString(mac) + ")");

        // Real captured material (cleartext usernames), so it survives
        // an unclean session end - same live-append convention as
        // ArpSpoofManager's /mitm/harvest.csv and EvilTwinManager's
        // associations.csv.
        fs::FS& fs = sdcard::exportFs();
        fs.mkdir("/eap");
        File f = fs.open("/eap/identities.csv", "a");
        if (f) {
            f.println(String(millis() / 1000) + "," + macToString(mac) + "," + identity);
            f.close();
        }
    }
}

void EapIdentityHarvester::notify(const String& text) {
    if (!_outQueue) return;
    ScanNotification n;
    n.source = ScanSource::EapIdentity;
    n.type = ScanEventType::LogLine;
    n.setText(text.c_str());
    xQueueSend(_outQueue, &n, 0);
}

size_t EapIdentityHarvester::count() const {
    if (!_mutex || xSemaphoreTake(_mutex, pdMS_TO_TICKS(100)) != pdTRUE) return 0;
    size_t n = _sightings.size();
    xSemaphoreGive(_mutex);
    return n;
}

bool EapIdentityHarvester::get(size_t index, Sighting& out) const {
    if (!_mutex || xSemaphoreTake(_mutex, pdMS_TO_TICKS(100)) != pdTRUE) return false;
    bool ok = index < _sightings.size();
    if (ok) out = _sightings[_sightings.size() - 1 - index];
    xSemaphoreGive(_mutex);
    return ok;
}
