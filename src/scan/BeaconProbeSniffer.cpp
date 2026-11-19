#include "BeaconProbeSniffer.h"
#include "../net/PromiscDispatcher.h"
#include "../core/ScanLog.h"
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

// WSC attribute parsing now lives in net/WpsInfo.h (wps::parseAttributes),
// shared with the active WPS probe and unit-tested on the host - findWpsIe
// above still locates the IE, that helper reads its TLVs.

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

        int sub = promisc::subscribe(promisc::kMgmt, &BeaconProbeSniffer::onDispatchedFrame);
        if (sub < 0) {
            notify("radio unavailable - not listening");
            _running = false;
            continue;
        }
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
                // Active WPS probe (opt-in): having just settled on a new
                // channel, send a wildcard probe request so APs here answer
                // with a probe response (and its WPS IE) now, instead of
                // waiting for one of their sparse beacons to land in our
                // dwell window. See setActiveProbe / the header invariant.
                if (_activeProbe) sendWildcardProbe();
            }
            vTaskDelay(pdMS_TO_TICKS(50));
        }

        promisc::unsubscribe(sub);
        notify("beacon/probe intel: stopped, reconnecting");
        // Channel-hopping necessarily broke any STA association this
        // device had - see the header's own-connection-disruption note.
        // No-op if nothing was saved.
        g_wifi.autoConnect();
    }
}

// Runs on the shared promiscuous dispatcher task (net/PromiscDispatcher.h),
// not the WiFi driver task - so the handler may lock/allocate.
void BeaconProbeSniffer::onDispatchedFrame(const promisc::Frame& f) { g_beaconProbeSniffer.onManagementFrame(f.data, f.len, f.rssi, f.channel); }

void BeaconProbeSniffer::onManagementFrame(const uint8_t* p, uint16_t len, int8_t rssi, uint8_t rxChannel) {
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
        handleApFrame(addr3, 24 + 12, p, len, privacy, rssi, rxChannel);
    } else if (subtype == 4) {
        // Probe Request: no fixed fields at all - tagged parameters start
        // right after the 24-byte MAC header. addr2 is the sender (the
        // client asking), not addr3 (broadcast ff:ff:ff:ff:ff:ff for an
        // untargeted probe, or a specific BSSID for a directed one).
        handleProbeRequest(addr2, 24, p, len, rssi);
    }
}

void BeaconProbeSniffer::handleApFrame(const uint8_t bssid[6], uint16_t bodyStart, const uint8_t* p, uint16_t len,
                                        bool privacy, int8_t rssi, uint8_t rxChannel) {
    String ssid;
    bool hasSsid = false;
    uint16_t ssidOff;
    uint8_t ssidLen;
    if (findIe(p, len, bodyStart, /*tag SSID=*/0, ssidOff, ssidLen) && ssidLen > 0) {
        hasSsid = true;
        ssid = decodeSsidBytes(p + ssidOff, ssidLen);
    }

    // Fallback when the beacon carries no DS Parameter Set: the channel
    // the radio received it on. Frames are now processed off a queue
    // (net/PromiscDispatcher.h), so by the time this runs the hop loop
    // may already have moved _currentChannel on - use the frame's own.
    uint8_t channel = rxChannel ? rxChannel : _currentChannel.load();
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

    wps::Info wpsInfo;
    uint16_t wpsOff;
    uint8_t wpsLen;
    if (findWpsIe(p, len, bodyStart, wpsOff, wpsLen)) {
        wpsInfo = wps::parseAttributes(p + wpsOff, wpsLen);
        wpsInfo.present = true;
    }

    updateAp(bssid, ssid, hasSsid, channel, enc, rssi, wpsInfo);
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
                                   wifi_auth_mode_t enc, int8_t rssi, const wps::Info& wpsInfo) {
    String bssidStr = macToString(bssid);

    bool isNewAp = false;
    bool revealedNow = false;
    bool wpsNewlySeen = false;
    bool pinCandidateNow = false;
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
            // A probe response often carries a richer/updated WPS IE than
            // the beacon did (the whole point of an active WPS probe), so
            // only overwrite with a present IE - don't let a WPS-less frame
            // wipe what an earlier frame already told us.
            if (wpsInfo.present) {
                bool wasPin = wps::candidacy(existing->wps) == wps::Candidacy::PinCandidate;
                if (!existing->wps.present) wpsNewlySeen = true;
                existing->wps = wpsInfo;
                bool isPin = wps::candidacy(wpsInfo) == wps::Candidacy::PinCandidate;
                if (isPin && !wasPin) pinCandidateNow = true;
            }
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
            rec.wps = wpsInfo;
            rec.firstSeenMs = millis();
            rec.lastSeenMs = rec.firstSeenMs;
            rec.beaconCount = 1;
            g_ouiDb.lookup(bssid, rec.vendor);
            _aps.push_back(rec);
            isNewAp = true;  // covers WPS too - "AP: ..." below is the only notify a brand-new AP needs
        }
        xSemaphoreGive(_mutex);
    }

    String apName = rec.hidden ? String("<hidden>") : rec.ssid;
    if (isNewAp) {
        notify(String("AP: ") + apName + " " + bssidStr);
        // A brand-new AP that's already a PIN candidate is worth calling
        // out on sight (the isNewAp notify above doesn't mention WPS).
        if (wps::candidacy(rec.wps) == wps::Candidacy::PinCandidate) notify("WPS-PIN candidate: " + apName);
    }
    if (revealedNow) notify("hidden SSID revealed: " + rec.ssid + " (" + bssidStr + ")");
    if (!isNewAp && wpsNewlySeen) notify("WPS enabled: " + apName + " " + bssidStr);
    if (!isNewAp && pinCandidateNow) notify("WPS-PIN candidate: " + apName + " " + bssidStr);
}

void BeaconProbeSniffer::sendWildcardProbe() {
    // Minimal, standard wildcard probe request (IEEE 802.11 mgmt subtype
    // 4): broadcast DA/BSSID, our own SA, an empty SSID element (wildcard
    // = "any network, answer me") and a basic Supported Rates element so
    // APs treat it as a well-formed request. Carries no target and no
    // payload - see the header's invariant note on why this one transmit
    // needs no gate. Not verified against real hardware (same standing
    // caveat as every other tx path here - see DeauthManager.h's RISK).
    uint8_t self[6];
    esp_wifi_get_mac(WIFI_IF_STA, self);

    uint8_t frame[] = {
        0x40, 0x00,                          // FC: mgmt, subtype 4 (probe request)
        0x00, 0x00,                          // duration
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff,  // addr1 DA = broadcast
        self[0], self[1], self[2], self[3], self[4], self[5],  // addr2 SA = us
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff,  // addr3 BSSID = broadcast
        0x00, 0x00,                          // seq ctrl (stack fills with en_sys_seq=true)
        0x00, 0x00,                          // IE: SSID, len 0 (wildcard)
        0x01, 0x04, 0x02, 0x04, 0x0b, 0x16,  // IE: Supported Rates (1,2,5.5,11 Mbps)
    };
    esp_wifi_80211_tx(WIFI_IF_STA, frame, sizeof(frame), /*en_sys_seq=*/true);
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

void BeaconProbeSniffer::notify(const String& text) { scanlog::post(_outQueue, ScanSource::BeaconProbe, text); }

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
