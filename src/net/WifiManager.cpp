#include "WifiManager.h"
#include "IpUtil.h"
#include <WiFi.h>

#if __has_include("secrets.h")
#include "secrets.h"
#else
#warning "include/secrets.h not found - copy include/secrets.h.example to include/secrets.h and fill in your WiFi credentials"
#define WIFI_SSID ""
#define WIFI_PASSWORD ""
#endif

WifiManager g_wifi;

void WifiManager::beginConnect() {
    if (WiFi.status() == WL_CONNECTED) return;
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}

bool WifiManager::connect(uint32_t timeoutMs) {
    if (WiFi.status() == WL_CONNECTED) return true;

    beginConnect();

    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - start) < timeoutMs) {
        vTaskDelay(pdMS_TO_TICKS(200));
    }
    return WiFi.status() == WL_CONNECTED;
}

bool WifiManager::isConnected() const { return WiFi.status() == WL_CONNECTED; }
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
