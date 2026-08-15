#pragma once

#include <M5GFX.h>

// Shared header widget that surfaces the global state the per-screen chrome
// otherwise hides: any long-running scan/sweep that keeps going after you
// navigate away from the screen that started it. Two buckets, since they
// carry different meaning:
//   RF:<name>   one promiscuous-mode feature owns the WiFi radio. esp_wifi
//   RF:N!       allows only ONE promiscuous callback at a time, so N>=2
//               active at once (ARP/MITM, deauth, PMKID, CDP-LLDP, rogue
//               DHCP, passive-host-discovery, beacon/probe intel, guard
//               mode, sentinel mode) is a real conflict, shown in red
//               instead of amber.
//   BG:<name>   one OTHER background sweep is running (war driving,
//   BG:N        NETWORK/PORT/SERVICE scans, SNMP/LDAP/NTLM/DataStore/SMB/
//               cred sweeps, evil-twin, ...). These don't fight over the
//               radio callback the way the RF set does, but they're just
//               as invisible once you've left their screen.
// When both are active at once the RF tag wins the slot and the BG count
// is appended ("RF:DHCP+2") rather than trying to show two separate tags —
// there isn't room for both on a 240px header next to a long screen title.
// Kept out of Chrome.cpp itself so the central chrome code doesn't pull in
// every scan manager; drawn by chrome::drawHeader via this one call.
namespace activity {

// Right-aligns the indicator so it ENDS at rightX (typically just left of
// the battery block). Draws nothing when nothing is running.
void draw(M5Canvas& gfx, int16_t rightX, int16_t y);

}  // namespace activity
