#pragma once

#include <M5GFX.h>

// Shared header widget that surfaces the one piece of global state the
// per-screen chrome otherwise hides: which promiscuous-mode feature owns
// the WiFi radio right now. esp_wifi allows only ONE promiscuous callback
// at a time, so running two of ARP/MITM, deauth, PMKID, CDP-LLDP, rogue
// DHCP or passive-host-discovery at once silently starves all but the
// last — this indicator makes that visible on every screen:
//   (nothing)   no promiscuous feature active
//   RF:<name>   one active (amber)
//   RF:N!       N>=2 active at once -> conflict (red)
// Kept out of Chrome.cpp itself so the central chrome code doesn't pull
// in every scan manager; drawn by chrome::drawHeader via this one call.
namespace activity {

// Right-aligns the indicator so it ENDS at rightX (typically just left of
// the battery block). Draws nothing when no promiscuous feature is active.
void draw(M5Canvas& gfx, int16_t rightX, int16_t y);

}  // namespace activity
