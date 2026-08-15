#include "BeaconProbeSniffer.h"
#include "OuiDatabase.h"
#include "../core/Types.h"
#include "../net/WifiManager.h"
#include <cstring>

BeaconProbeSniffer g_beaconProbeSniffer;

namespace {

// Finds the FIRST occurrence of tag `tagId` in the standard 802.11
// tagged-parameter list (tag(1) + length(1) + value(length), repeated to
// the end of the frame) starting at `start`. Fails closed (returns
// false) on anything short/malformed rather than guessing — same
// convention as Ieee80211Frame.h's parseSnap and CdpLldpSniffer's TLV
// walks.
bool findIe(const uint8_t* p, uint16_t len, uint16_t start, uint8_t tagId, uint16_t& valOff, uint8_t& valLen) {
    uint32_t pos = start;
    while (pos + 2 <= len) {
        uint8_t tag = p[pos];
        uint8_t l = p[pos + 1];
        if (pos + 2 + l > len) return false;  // malformed - stop rather than read past the frame
        if (tag == tagId) {
            valOff = (uint16_t)(pos + 2);
            valLen = l;
            return true;
        }
        pos += 2 + l;
    }
    return false;
}

// Vendor-specific IE (tag 221) carrying the Microsoft OUI (00:50:F2) with
// sub-type 1 is the pre-802.11i "WPA1" element — RSN (tag 48, checked
// separately) is what WPA2/WPA3 actually use. A frame can carry more than
// one tag-221 IE (WMM, etc.), so this walks all of them rather than
// stopping at the first.
bool hasVendorWpaIe(const uint8_t* p, uint16_t len, uint16_t start) {
    uint32_t pos = start;
    while (pos + 2 <= len) {
        uint8_t tag = p[pos];
        uint8_t l = p[pos + 1];
        if (pos + 2 + l > len) return false;
        if (tag == 221 && l >= 4) {
            const uint8_t* v = p + pos + 2;
            if (v[0] == 0x00 && v[1] == 0x50 && v[2] == 0xF2 && v[3] == 0x01) return true;
        }
        pos += 2 + l;
    }
    return false;
}

// Same vendor-specific IE (tag 221), same Microsoft/WFA OUI (00:50:F2),
// but sub-type 4: the WPS (Wi-Fi Simple Config) attribute list. Detection
// only - this firmware never implements WPS registration or attempts a
// PIN against anything found here (Reaver/pixie-dust-style attacks are
// out of scope, same "detect, don't attack an unverified protocol"
// stance as the SSH/NTLM-relay exclusions elsewhere - see README).
// Returns the offset/length of the attribute list AFTER the 4-byte
// OUI+type header, ready for parseWpsAttributes() below.
bool findWpsIe(const uint8_t* p, uint16_t len, uint16_t start, uint16_t& valOff, uint8_t& valLen) {
    uint32_t pos = start;
    while (pos + 2 <= len) {
        uint8_t tag = p[pos];
        uint8_t l = p[pos + 1];
        if (pos + 2 + l > len) return false;
        if (tag == 221 && l >= 4) {
            const uint8_t* v = p + pos + 2;
            if (v[0] == 0x00 && v[1] == 0x50 && v[2] == 0xF2 && v[3] == 0x04) {
                valOff = (uint16_t)(pos + 2 + 4);
                valLen = (uint8_t)(l - 4);
                return true;
            }
        }
        pos += 2 + l;
    }
    return false;
}

// Walks the WSC (Wi-Fi Simple Config) attribute TLVs inside a WPS IE's
// payload (see findWpsIe): each is a 2-byte BIG-ENDIAN Attribute ID + a
// 2-byte BIG-ENDIAN Length + that many value bytes (opposite byte order
// from the 802.11 tagged-parameter IEs everything else here reads -
// WSC attributes come from the separate Wi-Fi Alliance WSC spec, not
// 802.11 itself). Only pulls the two attributes this firmware actually
// surfaces: AP Setup Locked (0x1057, 1 byte - set once too many wrong
// PINs have been tried, a sign someone already attempted a PIN attack
// here) and Config Methods (0x1008, 2 bytes - which setup methods the
// AP advertises, e.g. push-button vs. PIN-capable). Fails closed on
// anything short/malformed, same convention as findIe. Doesn't handle
// WPS IEs fragmented across multiple tag-221 elements (rare in
// practice for a beacon's worth of attributes - the ones checked here
// appear early in a typical real-world WPS IE).
void parseWpsAttributes(const uint8_t* p, uint16_t len, bool& locked, uint16_t& configMethods) {
    uint16_t pos = 0;
    while (pos + 4 <= len) {
        uint16_t id = ((uint16_t)p[pos] << 8) | p[pos + 1];
        uint16_t l = ((uint16_t)p[pos + 2] << 8) | p[pos + 3];
        if ((uint32_t)pos + 4 + l > len) return;  // malformed - stop rather than read past the IE
        if (id == 0x1057 && l >= 1) {
            locked = p[pos + 4] != 0;
        } else if (id == 0x1008 && l >= 2) {
            configMethods = ((uint16_t)p[pos + 4] << 8) | p[pos + 5];
        }
        pos += 4 + l;
    }
}

// Best-effort printable-ASCII decode of an SSID element's raw bytes — an
// SSID is technically an arbitrary byte string, not guaranteed text, so
// non-printable bytes are shown as '?' rather than risking control
// characters/garbage in the UI (same defensive stance CdpLldpSniffer
// takes with CDP/LLDP TLV text).
String decodeSsidBytes(const uint8_t* p, uint8_t len) {
    String s;
    for (uint8_t i = 0; i < len && i < 32; i++) {
        char c = (char)p[i];
        s += (c >= 32 && c < 127) ? c : '?';
    }
    return s;
}

}  // namespace

void BeaconProbeSniffer::begin(QueueHandle_t outQueue) {
    _mutex = xSemaphoreCreateMutex();
    _outQueue = outQueue;
    // Created once, runs forever - internally idles (checking _running)
    // rather than being created/destroyed per start()/stop() - same
    // pattern as WardrivingManager/CdpLldpSniffer.
    xTaskCreatePinnedToCore(&BeaconProbeSniffer::taskEntry, "bcnprobe", 8192, this, 1, nullptr, 0);
}

void BeaconProbeSniffer::start() { _running = true; }
void BeaconProbeSniffer::stop() { _running = false; }

void BeaconProbeSniffer::taskEntry(void* arg) {
    static_cast<BeaconProbeSniffer*>(arg)->run();
}

void BeaconProbeSniffer::run() {
    for (;;) {
        if (!_running) {
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }

        esp_wifi_set_promiscuous_rx_cb(&BeaconProbeSniffer::promiscuousRxTrampoline);
        esp_wifi_set_promiscuous(true);
        notify("beacon/probe intel: listening (hopping channels)");

        uint8_t channel = 1;
        esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
        _currentChannel = channel;
        uint32_t lastHopMs = millis();

        while (_running) {
            if (millis() - lastHopMs > kChannelDwellMs) {
                channel = (uint8_t)((channel % kMaxChannel) + 1);
                esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
                _currentChannel = channel;
                lastHopMs = millis();
            }
            vTaskDelay(pdMS_TO_TICKS(50));
        }

        esp_wifi_set_promiscuous(false);
        notify("beacon/probe intel: stopped, reconnecting");
        // Channel-hopping necessarily broke any STA association this
        // device had - see the header's own-connection-disruption note.
        // No-op if nothing was saved.
        g_wifi.autoConnect();
    }
}

void BeaconProbeSniffer::promiscuousRxTrampoline(void* buf, wifi_promiscuous_pkt_type_t type) {
    if (type != WIFI_PKT_MGMT) return;
    auto* pkt = static_cast<wifi_promiscuous_pkt_t*>(buf);
    if (!pkt) return;
    g_beaconProbeSniffer.onManagementFrame(pkt->payload, pkt->rx_ctrl.sig_len, pkt->rx_ctrl.rssi);
}

void BeaconProbeSniffer::onManagementFrame(const uint8_t* p, uint16_t len, int8_t rssi) {
    if (len < 24) return;

    uint8_t fc0 = p[0];
    uint8_t type = (fc0 >> 2) & 0x3;
    uint8_t subtype = (fc0 >> 4) & 0xF;
    if (type != 0) return;  // not a Management frame

    const uint8_t* addr2 = p + 10;  // TA/SA
    const uint8_t* addr3 = p + 16;  // BSSID

    if (subtype == 8 || subtype == 5) {
        // Beacon (8) / Probe Response (5): both share the same 12-byte
        // fixed body (Timestamp[8] + Beacon Interval[2] + Capability
        // Info[2]) ahead of the tagged parameters.
        if (len < 24 + 12) return;
        uint16_t capInfo = (uint16_t)p[24 + 10] | ((uint16_t)p[24 + 11] << 8);  // little-endian, per 802.11
        bool privacy = (capInfo & 0x0010) != 0;
        handleApFrame(addr3, 24 + 12, p, len, privacy, rssi);
    } else if (subtype == 4) {
        // Probe Request: no fixed fields at all - tagged parameters start
        // right after the 24-byte MAC header. addr2 is the sender (the
        // client asking), not addr3 (broadcast ff:ff:ff:ff:ff:ff for an
        // untargeted probe, or a specific BSSID for a directed one).
        handleProbeRequest(addr2, 24, p, len, rssi);
    }
}

void BeaconProbeSniffer::handleApFrame(const uint8_t bssid[6], uint16_t bodyStart, const uint8_t* p, uint16_t len,
                                        bool privacy, int8_t rssi) {
    String ssid;
    bool hasSsid = false;
    uint16_t ssidOff;
    uint8_t ssidLen;
    if (findIe(p, len, bodyStart, /*tag SSID=*/0, ssidOff, ssidLen) && ssidLen > 0) {
        hasSsid = true;
        ssid = decodeSsidBytes(p + ssidOff, ssidLen);
    }

    uint8_t channel = _currentChannel;
    uint16_t dsOff;
    uint8_t dsLen;
    if (findIe(p, len, bodyStart, /*tag DS Parameter Set=*/3, dsOff, dsLen) && dsLen >= 1) {
        channel = p[dsOff];
    }

    wifi_auth_mode_t enc = WIFI_AUTH_OPEN;
    if (privacy) {
        uint16_t rsnOff;
        uint8_t rsnLen;
        bool hasRsn = findIe(p, len, bodyStart, /*tag RSN=*/48, rsnOff, rsnLen);
        // Best-effort, same spirit as WardrivingManager's evil-twin
        // heuristic: RSN present -> WPA2/3 (not distinguishing further -
        // most WPA3 deployments still advertise a WPA2-compatible
        // "transition mode" AKM alongside SAE, so a clean WPA2-vs-WPA3
        // split isn't reliable from the IE alone); vendor WPA IE without
        // RSN -> WPA1; privacy bit set with neither -> WEP (the only
        // encrypted case pre-dating both).
        enc = hasRsn ? WIFI_AUTH_WPA2_PSK : (hasVendorWpaIe(p, len, bodyStart) ? WIFI_AUTH_WPA_PSK : WIFI_AUTH_WEP);
    }

    bool wpsEnabled = false;
    bool wpsLocked = false;
    uint16_t wpsConfigMethods = 0;
    uint16_t wpsOff;
    uint8_t wpsLen;
    if (findWpsIe(p, len, bodyStart, wpsOff, wpsLen)) {
        wpsEnabled = true;
        parseWpsAttributes(p + wpsOff, wpsLen, wpsLocked, wpsConfigMethods);
    }

    updateAp(bssid, ssid, hasSsid, channel, enc, rssi, wpsEnabled, wpsLocked, wpsConfigMethods);
}

void BeaconProbeSniffer::handleProbeRequest(const uint8_t clientMac[6], uint16_t bodyStart, const uint8_t* p,
                                             uint16_t len, int8_t rssi) {
    String ssid;
    bool hasSsid = false;
    uint16_t ssidOff;
    uint8_t ssidLen;
    // ssidLen == 0 is a wildcard probe ("is anybody there") - not a PNL
    // disclosure, deliberately not recorded as a probed SSID.
    if (findIe(p, len, bodyStart, /*tag SSID=*/0, ssidOff, ssidLen) && ssidLen > 0) {
        hasSsid = true;
        ssid = decodeSsidBytes(p + ssidOff, ssidLen);
    }

    updateClient(clientMac, ssid, hasSsid, rssi);
}

void BeaconProbeSniffer::updateAp(const uint8_t bssid[6], const String& ssid, bool hasSsid, uint8_t channel,
                                   wifi_auth_mode_t enc, int8_t rssi, bool wpsEnabled, bool wpsLocked,
                                   uint16_t wpsConfigMethods) {
    String bssidStr = macToString(bssid);

    bool isNewAp = false;
    bool revealedNow = false;
    bool wpsNewlySeen = false;
    ApBeacon rec;

    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
        ApBeacon* existing = nullptr;
        for (auto& a : _aps) {
            if (a.bssid == bssidStr) {
                existing = &a;
                break;
            }
        }

        if (existing) {
            existing->lastSeenMs = millis();
            existing->beaconCount++;
            existing->rssi = rssi;
            existing->channel = channel;
            existing->encryption = enc;
            if (wpsEnabled && !existing->wpsEnabled) wpsNewlySeen = true;
            existing->wpsEnabled = wpsEnabled;
            existing->wpsLocked = wpsLocked;
            existing->wpsConfigMethods = wpsConfigMethods;
            if (hasSsid) {
                if (existing->hidden && existing->ssid.isEmpty()) {
                    existing->hiddenRevealed = true;
                    revealedNow = true;
                }
                existing->ssid = ssid;
                existing->hidden = false;
            } else if (existing->ssid.isEmpty()) {
                existing->hidden = true;
            }
            rec = *existing;
        } else if (_aps.size() < kMaxAps) {
            rec.bssid = bssidStr;
            rec.ssid = ssid;
            rec.hidden = !hasSsid;
            rec.channel = channel;
            rec.encryption = enc;
            rec.rssi = rssi;
            rec.wpsEnabled = wpsEnabled;
            rec.wpsLocked = wpsLocked;
            rec.wpsConfigMethods = wpsConfigMethods;
            rec.firstSeenMs = millis();
            rec.lastSeenMs = rec.firstSeenMs;
            rec.beaconCount = 1;
            g_ouiDb.lookup(bssid, rec.vendor);
            _aps.push_back(rec);
            isNewAp = true;  // covers WPS too - "AP: ..." below is the only notify a brand-new AP needs
        }
        xSemaphoreGive(_mutex);
    }

    if (isNewAp) notify(String("AP: ") + (rec.hidden ? String("<hidden>") : rec.ssid) + " " + bssidStr);
    if (revealedNow) notify("hidden SSID revealed: " + rec.ssid + " (" + bssidStr + ")");
    if (!isNewAp && wpsNewlySeen) notify("WPS enabled: " + (rec.hidden ? String("<hidden>") : rec.ssid) + " " + bssidStr);
}

void BeaconProbeSniffer::updateClient(const uint8_t mac[6], const String& probedSsid, bool hasSsid, int8_t rssi) {
    String macStr = macToString(mac);
    bool randomized = (mac[0] & 0x02) != 0;  // IEEE 802 locally-administered bit

    bool isNewClient = false;
    bool newSsidForClient = false;
    ProbeClient rec;

    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
        ProbeClient* existing = nullptr;
        for (auto& c : _clients) {
            if (c.mac == macStr) {
                existing = &c;
                break;
            }
        }

        if (existing) {
            existing->lastSeenMs = millis();
            existing->probeCount++;
            existing->lastRssi = rssi;
            if (hasSsid) {
                bool dup = false;
                for (const auto& s : existing->probedSsids) {
                    if (s == probedSsid) {
                        dup = true;
                        break;
                    }
                }
                if (!dup && existing->probedSsids.size() < kMaxProbedSsidsPerClient) {
                    existing->probedSsids.push_back(probedSsid);
                    newSsidForClient = true;
                }
            }
            rec = *existing;
        } else if (_clients.size() < kMaxClients) {
            rec.mac = macStr;
            rec.macRandomized = randomized;
            if (!randomized) g_ouiDb.lookup(mac, rec.vendor);
            rec.firstSeenMs = millis();
            rec.lastSeenMs = rec.firstSeenMs;
            rec.probeCount = 1;
            rec.lastRssi = rssi;
            if (hasSsid) {
                rec.probedSsids.push_back(probedSsid);
                newSsidForClient = true;
            }
            _clients.push_back(rec);
            isNewClient = true;
        }
        xSemaphoreGive(_mutex);
    }

    if (isNewClient) {
        notify(String("client: ") + macStr + (randomized ? " (randomized)" : ""));
    } else if (newSsidForClient) {
        notify("PNL: " + macStr + " probed \"" + probedSsid + "\"");
    }
}

void BeaconProbeSniffer::notify(const String& text) {
    if (!_outQueue) return;
    ScanNotification n;
    n.source = ScanSource::BeaconProbe;
    n.type = ScanEventType::LogLine;
    n.setText(text.c_str());
    xQueueSend(_outQueue, &n, 0);
}

size_t BeaconProbeSniffer::apCount() const {
    if (!_mutex || xSemaphoreTake(_mutex, pdMS_TO_TICKS(100)) != pdTRUE) return 0;
    size_t n = _aps.size();
    xSemaphoreGive(_mutex);
    return n;
}

bool BeaconProbeSniffer::getAp(size_t index, ApBeacon& out) const {
    if (!_mutex || xSemaphoreTake(_mutex, pdMS_TO_TICKS(100)) != pdTRUE) return false;
    bool ok = index < _aps.size();
    if (ok) out = _aps[_aps.size() - 1 - index];
    xSemaphoreGive(_mutex);
    return ok;
}

size_t BeaconProbeSniffer::clientCount() const {
    if (!_mutex || xSemaphoreTake(_mutex, pdMS_TO_TICKS(100)) != pdTRUE) return 0;
    size_t n = _clients.size();
    xSemaphoreGive(_mutex);
    return n;
}

bool BeaconProbeSniffer::getClient(size_t index, ProbeClient& out) const {
    if (!_mutex || xSemaphoreTake(_mutex, pdMS_TO_TICKS(100)) != pdTRUE) return false;
    bool ok = index < _clients.size();
    if (ok) out = _clients[_clients.size() - 1 - index];
    xSemaphoreGive(_mutex);
    return ok;
}
