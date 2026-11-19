#include "Config.h"
#include <Preferences.h>

AppConfig g_config;

namespace {
constexpr const char* kNamespace = "netaudit";
}

void AppConfig::load() {
    Preferences prefs;
    if (!prefs.begin(kNamespace, /*readOnly=*/true)) {
        // No namespace saved yet (first boot) — keep in-struct defaults.
        return;
    }

    uint32_t subnet = prefs.getUInt("subnet", (uint32_t)subnetBase);
    subnetBase = IPAddress(subnet);
    subnetPrefix = prefs.getUChar("prefix", subnetPrefix);
    portRangeStart = prefs.getUShort("portStart", portRangeStart);
    portRangeEnd = prefs.getUShort("portEnd", portRangeEnd);
    scanTimeoutMs = prefs.getUShort("scanTimeout", scanTimeoutMs);
    maxConcurrentProbes = prefs.getUChar("maxProbes", maxConcurrentProbes);
    interProbeDelayMs = prefs.getUShort("probeDelay", interProbeDelayMs);
    autoExportOnScanFinish = prefs.getBool("autoExport", autoExportOnScanFinish);
    credAuditAcknowledged = prefs.getBool("credAck", credAuditAcknowledged);
    // credAuditEnabled is intentionally NOT persisted: every boot starts
    // with the audit module off, even if it was acknowledged/used before.
    offensiveAcknowledged = prefs.getBool("offAck", offensiveAcknowledged);
    // offensiveEnabled likewise never persisted - see its declaration.
    uiSoundEnabled = prefs.getUChar("sound", uiSoundEnabled);
    lowPowerMode = prefs.getUChar("lowpow", lowPowerMode);

    prefs.end();

    clamp();
}

void AppConfig::clamp() {
    // Same bounds as SettingsScreen::adjust().
    if (scanTimeoutMs < 50) scanTimeoutMs = 50;
    if (scanTimeoutMs > 5000) scanTimeoutMs = 5000;
    if (maxConcurrentProbes < 1) maxConcurrentProbes = 1;
    if (maxConcurrentProbes > 8) maxConcurrentProbes = 8;
    // A 0 delay makes the scan/credential loops call vTaskDelay(0), which
    // on FreeRTOS does NOT yield - it starves lower-priority tasks and can
    // trip the task watchdog.
    if (interProbeDelayMs < kMinInterProbeDelayMs) interProbeDelayMs = kMinInterProbeDelayMs;
    if (interProbeDelayMs > 200) interProbeDelayMs = 200;
    if (portRangeStart < 1) portRangeStart = 1;
    if (portRangeEnd < portRangeStart) portRangeEnd = portRangeStart;
    if (subnetPrefix < 1 || subnetPrefix > 32) subnetPrefix = 24;
    uiSoundEnabled = uiSoundEnabled ? 1 : 0;
    lowPowerMode = lowPowerMode ? 1 : 0;
}

void AppConfig::save() const {
    Preferences prefs;
    if (!prefs.begin(kNamespace, /*readOnly=*/false)) return;

    prefs.putUInt("subnet", (uint32_t)subnetBase);
    prefs.putUChar("prefix", subnetPrefix);
    prefs.putUShort("portStart", portRangeStart);
    prefs.putUShort("portEnd", portRangeEnd);
    prefs.putUShort("scanTimeout", scanTimeoutMs);
    prefs.putUChar("maxProbes", maxConcurrentProbes);
    prefs.putUShort("probeDelay", interProbeDelayMs);
    prefs.putBool("autoExport", autoExportOnScanFinish);
    prefs.putBool("credAck", credAuditAcknowledged);
    prefs.putBool("offAck", offensiveAcknowledged);
    prefs.putUChar("sound", uiSoundEnabled);
    prefs.putUChar("lowpow", lowPowerMode);

    prefs.end();
}

void AppConfig::resetToDefaults() {
    *this = AppConfig{};
}
