#include "PingSweep.h"
#include <WiFi.h>

namespace {
// Common enough to catch most live hosts (web UI, HTTPS, SSH, SMB)
// without paying the worst-case timeout for many ports per dead host.
// Kept short on purpose: PingSweep::probe's worst case (a genuinely
// unreachable IP) costs kProbePortCount * timeoutMs, and that cost is
// paid for every dead address in the subnet.
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
    }
    return false;
}
