#pragma once

#include <Arduino.h>
#include <IPAddress.h>
#include <cstring>
#include <vector>

// Shared data model used by scan/, net/, ui/ and storage/. Kept in one
// place so every module agrees on what a "host" or "port result" is.

enum class DeviceClass : uint8_t {
    Unknown = 0,
    Router,
    IoT,
    Mobile,
    Computer,
    Printer,
    MediaServer,
};

const char* deviceClassLabel(DeviceClass c);

// Risk drives the dashboard row color (green/yellow/red). It is a coarse
// heuristic, not a vulnerability score: Critical means "known-bad thing
// found" (default creds, telnet open), Warning means "worth a look"
// (unexpected open port), Ok means "nothing flagged yet".
enum class RiskLevel : uint8_t { Ok = 0, Warning = 1, Critical = 2 };

struct PortResult {
    uint16_t port = 0;
    bool open = false;
    String service;  // short protocol guess, e.g. "http", "ssh"
    String banner;    // first line grabbed from the service, truncated
};

struct HostInfo {
    IPAddress ip;
    uint8_t mac[6] = {0, 0, 0, 0, 0, 0};
    bool macKnown = false;
    String hostname;
    String vendor;
    DeviceClass deviceClass = DeviceClass::Unknown;
    RiskLevel risk = RiskLevel::Ok;
    bool alive = false;
    uint32_t lastSeenMs = 0;
    std::vector<PortResult> ports;
    bool credAudited = false;
    bool credVulnerable = false;
    String credNote;

    bool macEqual(const uint8_t other[6]) const {
        return memcmp(mac, other, 6) == 0;
    }
};

String macToString(const uint8_t mac[6]);
