#include "PingSweep.h"
#include "ArpResolver.h"
#include <WiFi.h>

namespace {
// Common enough to catch most live hosts (web UI, HTTPS, SSH, SMB)
// without paying the worst-case timeout for many ports per dead host.
// Kept short on purpose: PingSweep::probe's worst case (a genuinely
// unreachable IP) used to cost kProbePortCount * timeoutMs, paid for
// every dead address in the subnet - see the ARP short-circuit below
// for why that worst case is now ~1x instead.
constexpr uint16_t kProbePorts[] = {80, 443, 22, 445};
constexpr size_t kProbePortCount = sizeof(kProbePorts) / sizeof(kProbePorts[0]);
}  // namespace

// Why TCP connect-scan instead of ICMP ping: a raw SYN scan isn't
// achievable through lwIP's standard socket API without patching the
// stack (true on both Arduino and ESP-IDF — see README "Scelte
// tecniche"), and there's no version-stable ICMP ping API this codebase
// wants to depend on. A short TCP connect attempt on a handful of common
// ports uses the well-documented, stable WiFiClient API, and — as a
// useful side effect of lwIP resolving the destination MAC to send the
// SYN — populates the ARP cache exactly like a ping would, which is the
// other thing this sweep needs to happen anyway (see ArpResolver).
bool PingSweep::probe(const IPAddress& ip, uint16_t timeoutMs) {
    for (size_t i = 0; i < kProbePortCount; i++) {
        WiFiClient client;
        bool ok = client.connect(ip, kProbePorts[i], timeoutMs);
        client.stop();
        if (ok) return true;

        // connect() above had to ARP-resolve ip to even send the SYN, so
        // after the first miss, a still-empty ARP entry means nothing on
        // the LAN answered that resolution at all - almost always "no
        // host at this address", not "host up but this port closed/
        // filtered" (a live host that merely refuses/ignores a port
        // still answers ARP). Bailing out here instead of trying the
        // remaining kProbePortCount-1 ports turns the worst case for a
        // dead address - the overwhelming majority of any real subnet -
        // from kProbePortCount*timeoutMs down to ~1*timeoutMs.
        if (i == 0) {
            uint8_t mac[6];
            if (!ArpResolver::lookupMac(ip, mac)) return false;
        }
    }
    return false;
}
