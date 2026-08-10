#include "ArpResolver.h"
#include <WiFi.h>
#include <cstring>

extern "C" {
#include "lwip/etharp.h"
#include "esp_netif.h"
}

namespace ArpResolver {

bool lookupMac(const IPAddress& ip, uint8_t mac[6]) {
    // "WIFI_STA_DEF" is the ifkey arduino-esp32 registers the station
    // interface under internally (via ESP-IDF's
    // esp_netif_create_default_wifi_sta()) — going through this stable
    // ESP-IDF lookup instead of Arduino's WiFiSTAClass keeps this file's
    // only real dependency on ESP-IDF itself, not on whichever
    // arduino-esp32 wrapper method happens to exist in a given release.
    esp_netif_t* espNetif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (!espNetif) return false;

    // esp_netif is ESP-IDF's abstraction layer; etharp_find_addr() is a
    // plain lwIP call and wants the underlying lwIP `struct netif*`.
    // esp_netif_get_netif_impl() is the documented way to get from one
    // to the other.
    auto* lwipNetif = static_cast<struct netif*>(esp_netif_get_netif_impl(espNetif));
    if (!lwipNetif) return false;

    ip4_addr_t target;
    target.addr = static_cast<uint32_t>(ip);

    struct eth_addr* ethRet = nullptr;
    const ip4_addr_t* ipRet = nullptr;
    if (etharp_find_addr(lwipNetif, &target, &ethRet, &ipRet) < 0 || ethRet == nullptr) {
        return false;
    }

    memcpy(mac, ethRet->addr, 6);
    return true;
}

}  // namespace ArpResolver
