#include "WifiManager.h"
#include "IpUtil.h"
#include <Preferences.h>
#include <WiFi.h>

WifiManager g_wifi;

namespace {
constexpr const char* kNvsNamespace = "wifi";
}

bool WifiManager::hasSavedCredentials() const {
    Preferences prefs;
    if (!prefs.begin(kNvsNamespace, /*readOnly=*/true)) return false;
    bool has = prefs.isKey("ssid");
    prefs.end();
    return has;
}

String WifiManager::savedSsid() const {
    Preferences prefs;
    if (!prefs.begin(kNvsNamespace, /*readOnly=*/true)) return "";
    String s = prefs.getString("ssid", "");
    prefs.end();
    return s;
}

void WifiManager::saveCredentials(const String& ssid, const String& password) {
    Preferences prefs;
    if (!prefs.begin(kNvsNamespace, /*readOnly=*/false)) return;
    prefs.putString("ssid", ssid);
    prefs.putString("pass", password);
    prefs.end();
}

void WifiManager::forgetSavedCredentials() {
    Preferences prefs;
    if (!prefs.begin(kNvsNamespace, /*readOnly=*/false)) return;
    prefs.clear();
    prefs.end();
    WiFi.disconnect(/*wifioff=*/false, /*eraseap=*/true);
}

bool WifiManager::autoConnect() {
    Preferences prefs;
    if (!prefs.begin(kNvsNamespace, /*readOnly=*/true)) return false;
    String ssid = prefs.getString("ssid", "");
    String pass = prefs.getString("pass", "");
    prefs.end();

    if (ssid.isEmpty()) return false;

    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), pass.c_str());
    return true;
}

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
    // async=true: returns immediately with kScanRunning: the UI task
    // polls scanStatus() instead of blocking the render loop on a scan
    // that can take a couple of seconds.
    WiFi.scanNetworks(/*async=*/true);
}

int16_t WifiManager::scanStatus() const {
    return WiFi.scanComplete();
}

bool WifiManager::getScanResult(int16_t index, ScanResult& out) const {
    int16_t count = WiFi.scanComplete();
    if (count < 0 || index < 0 || index >= count) return false;

    out.ssid = WiFi.SSID(index);
    out.rssi = WiFi.RSSI(index);
    out.encryption = WiFi.encryptionType(index);
    return true;
}
