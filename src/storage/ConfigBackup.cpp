#include "ConfigBackup.h"
#include "../core/Config.h"
#include "../net/WifiManager.h"
#include "../scan/WardrivingManager.h"
#include <ArduinoJson.h>

bool ConfigBackup::backup(fs::FS& fs, const char* path) {
    File f = fs.open(path, "w");
    if (!f) return false;

    JsonDocument doc;

    JsonObject cfg = doc["config"].to<JsonObject>();
    cfg["scanTimeoutMs"] = g_config.scanTimeoutMs;
    cfg["maxConcurrentProbes"] = g_config.maxConcurrentProbes;
    cfg["interProbeDelayMs"] = g_config.interProbeDelayMs;
    cfg["portRangeStart"] = g_config.portRangeStart;
    cfg["portRangeEnd"] = g_config.portRangeEnd;
    cfg["autoExportOnScanFinish"] = g_config.autoExportOnScanFinish;
    cfg["uiSoundEnabled"] = g_config.uiSoundEnabled;

    // Written oldest-to-most-recently-used, matching storage order -
    // restore() reinserts in the same order via saveCredentials(), so
    // the MRU ordering round-trips.
    JsonArray networks = doc["wifiNetworks"].to<JsonArray>();
    uint8_t wifiCount = g_wifi.savedNetworkCount();
    for (uint8_t i = 0; i < wifiCount; i++) {
        JsonObject n = networks.add<JsonObject>();
        n["ssid"] = g_wifi.savedNetworkSsid(i);
        n["pass"] = g_wifi.savedNetworkPassword(i);
    }

    JsonArray allowlist = doc["wardrivingAllowlist"].to<JsonArray>();
    uint8_t alCount = g_wardrivingManager.allowlistCount();
    for (uint8_t i = 0; i < alCount; i++) {
        allowlist.add(g_wardrivingManager.allowlistSsid(i));
    }

    serializeJson(doc, f);
    f.close();
    return true;
}

bool ConfigBackup::restore(fs::FS& fs, const char* path) {
    File f = fs.open(path, "r");
    if (!f) return false;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    if (err) return false;

    JsonObject cfg = doc["config"].as<JsonObject>();
    g_config.scanTimeoutMs = cfg["scanTimeoutMs"] | g_config.scanTimeoutMs;
    g_config.maxConcurrentProbes = cfg["maxConcurrentProbes"] | g_config.maxConcurrentProbes;
    g_config.interProbeDelayMs = cfg["interProbeDelayMs"] | g_config.interProbeDelayMs;
    g_config.portRangeStart = cfg["portRangeStart"] | g_config.portRangeStart;
    g_config.portRangeEnd = cfg["portRangeEnd"] | g_config.portRangeEnd;
    g_config.autoExportOnScanFinish = cfg["autoExportOnScanFinish"] | g_config.autoExportOnScanFinish;
    g_config.uiSoundEnabled = cfg["uiSoundEnabled"] | g_config.uiSoundEnabled;
    g_config.save();

    g_wifi.forgetSavedCredentials();  // clears all saved networks first - see header
    JsonArray networks = doc["wifiNetworks"].as<JsonArray>();
    size_t netCount = networks.size();
    // Reversed: saveCredentials() always inserts at the front, so
    // inserting oldest-first reconstructs the original MRU order.
    for (size_t i = netCount; i-- > 0;) {
        JsonObject n = networks[i];
        String ssid = n["ssid"] | "";
        String pass = n["pass"] | "";
        if (ssid.length()) g_wifi.saveCredentials(ssid, pass);
    }

    while (g_wardrivingManager.allowlistCount() > 0) {
        g_wardrivingManager.removeFromAllowlist(0);
    }
    JsonArray allowlist = doc["wardrivingAllowlist"].as<JsonArray>();
    for (JsonVariant v : allowlist) {
        String ssid = v.as<String>();
        if (ssid.length()) g_wardrivingManager.addToAllowlist(ssid);
    }

    return true;
}
