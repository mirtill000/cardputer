#include "ArpResolver.h"
#include <WiFi.h>
#include <cstring>

extern "C" {
#include "lwip/etharp.h"
#include "lwip/netif.h"
}

// EDIT (post-review, build-verified on real hardware/toolchain): the
// original version of this file went through esp_netif
// (esp_netif_get_handle_from_ifkey() + esp_netif_get_netif_impl()) to
// reach lwIP's struct netif*. esp_netif_get_netif_impl() doesn't exist
// in the esp-idf version this project's pinned espressif32 platform
// bundles ("was not declared in this scope" at compile time) — exactly
// the kind of version-sensitivity this file's original comment warned
// about. Switched to lwIP's own `netif_default` global instead: it's
// plain lwIP (declared in lwip/netif.h, not an ESP-IDF wrapper), so it
// doesn't depend on esp_netif's internal API surface at all, and for a
// STA-only device (no AP, no Ethernet) it reliably points at the WiFi
// interface — there's only one netif to be "default".

namespace ArpResolver {

bool lookupMac(const IPAddress& ip, uint8_t mac[6]) {
    struct netif* lwipNetif = netif_default;
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
