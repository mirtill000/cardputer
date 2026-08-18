#pragma once

#include <WiFiClient.h>
#include <cstdint>
#include "../core/Types.h"

// Best-effort service fingerprinting on an already-open TCP socket.
// Called right after PortScanner confirms a port is open, reusing that
// same connection rather than opening a second one.
namespace BannerGrabber {

// Fills service (a short protocol guess, e.g. "http") and banner (the
// first line of whatever the service said, truncated) based on `port`
// and whatever bytes the service sends/replies with. Never throws, never
// blocks longer than timeoutMs, and always leaves service/banner in a
// safe (possibly empty) state even if the service says nothing.
void grab(WiFiClient& client, uint16_t port, uint16_t timeoutMs, PortResult& result);

}  // namespace BannerGrabber
