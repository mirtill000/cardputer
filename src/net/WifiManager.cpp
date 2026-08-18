#include "WifiManager.h"
#include "IpUtil.h"
#include <Preferences.h>
#include <WiFi.h>

WifiManager g_wifi;

namespace {
constexpr const char* kNvsNamespace = "wifi";
}

// On-disk layout (NVS namespace "wifi"): "count" (uint8, 0..
// kMaxSavedNetworks) plus "ssid0"/"pass0" .. "ssid<N-1>"/"pass<N-1>",
// index 0 always the most-recently-used network. There's no separate
// migration for the single-network layout this used to have — a first
// save() under the new scheme just starts a fresh list, and any prior
// single "ssid"/"pass" keys are simply never read again (harmless
// orphaned NVS entries, not a crash risk).
namespace {
String slotKey(const char* prefix, uint8_t index) {
    return String(prefix) + String(index);
}
}  // namespace

bool WifiManager::hasSavedCredentials() const { return savedNetworkCount() > 0; }

String WifiManager::savedSsid() const { return savedNetworkSsid(0); }

void WifiManager::saveCredentials(const String& ssid, const String& password) {
    Preferences prefs;
    if (!prefs.begin(kNvsNamespace, /*readOnly=*/false)) return;

    uint8_t count = prefs.getUChar("count", 0);
    if (count > kMaxSavedNetworks) count = kMaxSavedNetworks;
    String oldSsids[kMaxSavedNetworks];
    String oldPasses[kMaxSavedNetworks];
    for (uint8_t i = 0; i < count; i++) {
        oldSsids[i] = prefs.getString(slotKey("ssid", i).c_str(), "");
        oldPasses[i] = prefs.getString(slotKey("pass", i).c_str(), "");
    }

    // New/updated entry always goes to the front; any existing entry
    // for the same SSID is dropped from its old position (dedup) rather
    // than kept as a stale duplicate further down the list.
    String newSsids[kMaxSavedNetworks];
    String newPasses[kMaxSavedNetworks];
    newSsids[0] = ssid;
    newPasses[0] = password;
    uint8_t writeCount = 1;
    for (uint8_t i = 0; i < count && writeCount < kMaxSavedNetworks; i++) {
        if (oldSsids[i] == ssid) continue;
        newSsids[writeCount] = oldSsids[i];
        newPasses[writeCount] = oldPasses[i];
        writeCount++;
    }

    for (uint8_t i = 0; i < writeCount; i++) {
        prefs.putString(slotKey("ssid", i).c_str(), newSsids[i]);
        prefs.putString(slotKey("pass", i).c_str(), newPasses[i]);
    }
    prefs.putUChar("count", writeCount);

    prefs.end();
}

void WifiManager::forgetSavedCredentials() {
    Preferences prefs;
    if (!prefs.begin(kNvsNamespace, /*readOnly=*/false)) return;
    prefs.clear();
    prefs.end();
    WiFi.disconnect(/*wifioff=*/false, /*eraseap=*/true);
}

uint8_t WifiManager::savedNetworkCount() const {
    Preferences prefs;
    if (!prefs.begin(kNvsNamespace, /*readOnly=*/true)) return 0;
    uint8_t count = prefs.getUChar("count", 0);
    prefs.end();
    return (count > kMaxSavedNetworks) ? kMaxSavedNetworks : count;
}

String WifiManager::savedNetworkSsid(uint8_t index) const {
    if (index >= savedNetworkCount()) return "";
    Preferences prefs;
    if (!prefs.begin(kNvsNamespace, /*readOnly=*/true)) return "";
    String s = prefs.getString(slotKey("ssid", index).c_str(), "");
    prefs.end();
    return s;
}

String WifiManager::savedNetworkPassword(uint8_t index) const {
    if (index >= savedNetworkCount()) return "";
    Preferences prefs;
    if (!prefs.begin(kNvsNamespace, /*readOnly=*/true)) return "";
    String s = prefs.getString(slotKey("pass", index).c_str(), "");
    prefs.end();
    return s;
}

bool WifiManager::connectSaved(uint8_t index) {
    if (index >= savedNetworkCount()) return false;
    Preferences prefs;
    if (!prefs.begin(kNvsNamespace, /*readOnly=*/true)) return false;
    String ssid = prefs.getString(slotKey("ssid", index).c_str(), "");
    String pass = prefs.getString(slotKey("pass", index).c_str(), "");
    prefs.end();
    if (ssid.isEmpty()) return false;

    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), pass.c_str());
    return true;
}

void WifiManager::touchSavedNetwork(uint8_t index) {
    if (index >= savedNetworkCount()) return;
    Preferences prefs;
    if (!prefs.begin(kNvsNamespace, /*readOnly=*/true)) return;
    String ssid = prefs.getString(slotKey("ssid", index).c_str(), "");
    String pass = prefs.getString(slotKey("pass", index).c_str(), "");
    prefs.end();
    if (ssid.isEmpty()) return;
    saveCredentials(ssid, pass);  // re-inserts at front with its own (unchanged) password
}

void WifiManager::forgetSavedNetwork(uint8_t index) {
    Preferences prefs;
    if (!prefs.begin(kNvsNamespace, /*readOnly=*/false)) return;
    uint8_t count = prefs.getUChar("count", 0);
    if (count > kMaxSavedNetworks) count = kMaxSavedNetworks;
    if (index >= count) {
        prefs.end();
        return;
    }

    String ssids[kMaxSavedNetworks];
    String passes[kMaxSavedNetworks];
    for (uint8_t i = 0; i < count; i++) {
        ssids[i] = prefs.getString(slotKey("ssid", i).c_str(), "");
        passes[i] = prefs.getString(slotKey("pass", i).c_str(), "");
    }

    uint8_t newCount = 0;
    for (uint8_t i = 0; i < count; i++) {
        if (i == index) continue;
        prefs.putString(slotKey("ssid", newCount).c_str(), ssids[i]);
        prefs.putString(slotKey("pass", newCount).c_str(), passes[i]);
        newCount++;
    }
    // Clear the now-unreferenced trailing slot - NVS hygiene, not a
    // correctness requirement (nothing ever reads index >= count).
    prefs.remove(slotKey("ssid", count - 1).c_str());
    prefs.remove(slotKey("pass", count - 1).c_str());
    prefs.putUChar("count", newCount);

    prefs.end();
}

bool WifiManager::autoConnect() { return connectSaved(0); }

void WifiManager::beginConnectWithCredentials(const String& ssid, const String& password) {
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), password.c_str());
}

bool WifiManager::isConnected() const { return WiFi.status() == WL_CONNECTED; }

bool WifiManager::connectFailed() const {
    wl_status_t s = WiFi.status();
    return s == WL_CONNECT_FAILED || s == WL_NO_SSID_AVAIL;
}

String WifiManager::currentSsid() const { return WiFi.SSID(); }
uint8_t WifiManager::currentChannel() const { return isConnected() ? (uint8_t)WiFi.channel() : 0; }
IPAddress WifiManager::localIP() const { return WiFi.localIP(); }
IPAddress WifiManager::subnetMask() const { return WiFi.subnetMask(); }
IPAddress WifiManager::gatewayIP() const { return WiFi.gatewayIP(); }

IPAddress WifiManager::networkAddress() const {
    uint32_t ipVal = iputil::toBigEndianValue(WiFi.localIP());
    uint32_t maskVal = iputil::toBigEndianValue(WiFi.subnetMask());
    return iputil::fromBigEndianValue(ipVal & maskVal);
}

uint32_t WifiManager::hostCount() const {
    uint32_t maskVal = iputil::toBigEndianValue(WiFi.subnetMask());
    uint8_t hostBits = iputil::popcount32(~maskVal);
    if (hostBits == 0 || hostBits >= 32) return 0;  // /32 (or a degenerate all-zero mask)

    uint32_t total = 1UL << hostBits;
    uint32_t usable = (total > 2) ? (total - 2) : 0;  // exclude network + broadcast addresses
    return (usable > kMaxScanHosts) ? kMaxScanHosts : usable;
}

void WifiManager::beginScan() {
    WiFi.mode(WIFI_STA);
    // A saved network's autoConnect()/connectSaved() may still be
    // retrying in the background if the AP wasn't reachable when it
    // tried (out of range, temporarily down, password changed on the
    // router) - the ESP32 WiFi driver keeps that connection attempt
    // alive and retries it on its own, which monopolizes the radio and
    // can make scanNetworks() below hang forever (scanComplete() stuck
    // at kScanRunning) or fail outright. Stop it first - but only when
    // we're NOT already successfully connected: an established
    // connection scans around it just fine on this hardware, no need to
    // drop a working session just to look for other networks. Mirrors
    // what forgetSavedCredentials() already does, minus erasing
    // anything - this is "stop trying for now", not "forget".
    if (!isConnected()) WiFi.disconnect(/*wifioff=*/false, /*eraseap=*/false);
    // async=true: returns immediately with kScanRunning: the UI task
    // polls scanStatus() instead of blocking the render loop on a scan
    // that can take a couple of seconds.
    //
    // show_hidden=true: networks that don't broadcast their SSID used
    // to be silently invisible to this scan entirely (WiFi.SSID(i) came
    // back "", and both callers of getScanResult() filtered blank SSIDs
    // out) - real war-driving tools log these too, keyed by BSSID since
    // that's all there is to identify them by. See WardrivingManager for
    // where a blank SSID now gets displayed as "<hidden>" instead of
    // being dropped.
    //
    // max_ms_per_chan=400 (default is 300): dwells longer per channel,
    // giving weak/slow-beaconing APs more of a chance to be caught in a
    // single sweep - trades a somewhat longer scan (a extra couple of
    // seconds across the full 2.4GHz channel set) for fewer missed
    // networks, which matters more for a tool whose job is finding
    // networks than it does for the sub-second scans of most phone UIs.
    WiFi.scanNetworks(/*async=*/true, /*show_hidden=*/true, /*passive=*/false, /*max_ms_per_chan=*/400);
}

int16_t WifiManager::scanStatus() const {
    return WiFi.scanComplete();
}

bool WifiManager::getScanResult(int16_t index, ScanResult& out) const {
    int16_t count = WiFi.scanComplete();
    if (count < 0 || index < 0 || index >= count) return false;

    out.ssid = WiFi.SSID(index);
    out.bssid = WiFi.BSSIDstr(index);
    out.rssi = WiFi.RSSI(index);
    out.channel = (uint8_t)WiFi.channel(index);
    out.encryption = WiFi.encryptionType(index);
    return true;
}
