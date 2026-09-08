#include "DeviceClassifier.h"

namespace {

const char* const kRouterKeywords[] = {
    "NETGEAR", "D-LINK", "MIKROTIK", "UBIQUITI", "CISCO", "ZYXEL",
    "AVM ", "ARRIS", "TECHNICOLOR", "SAGEMCOM", "TP-LINK",
    "ARUBA NETWORKS", "ACTIONTEC", "CALIX", "ADTRAN", "UBEE INTERACTIVE",
    "HITRON TECHNOLOGIES",
};
// Streaming/AV devices: split out of the old kIotKeywords bucket (Sonos/
// Roku moved here) since "MEDIA" is a strictly more useful label for
// them than the generic "IOT" every smart plug/sensor also gets.
const char* const kMediaKeywords[] = {
    "SONOS", "ROKU", "SYNOLOGY", "QNAP SYSTEMS",
};
const char* const kIotKeywords[] = {
    "ESPRESSIF", "RASPBERRY PI", "ITEAD", "TUYA", "SONOFF", "AMAZON",
    "NEST LABS", "PHILIPS", "RING LLC", "WYZE", "GOOGLE",
    "BELKIN", "ECOBEE", "ARLO", "SHELLY",
};
const char* const kMobileKeywords[] = {
    "APPLE", "SAMSUNG ELECTRO", "XIAOMI", "HUAWEI DEVICE", "OPPO", "ONEPLUS",
};
const char* const kPrinterKeywords[] = {
    "HEWLETT PACKARD", "HP INC", "CANON", "EPSON", "BROTHER INDUSTRIES",
    "XEROX", "LEXMARK", "KYOCERA", "RICOH",
};
const char* const kComputerKeywords[] = {
    "INTEL CORP", "DELL INC", "MICROSOFT", "LENOVO", "ASUSTEK COMPUTER",
    "REALTEK", "GIGA-BYTE",
};

// Second, independent signal on top of the vendor OUI lookup above -
// see the rationale in DeviceClassifier.h and the comment in classify()
// for when this is allowed to run/override. Hostnames come from NBNS/
// mDNS/DHCP, so this is naming the device announced itself, not
// hardware fact like the OUI - kept to patterns specific enough that a
// false match is unlikely.
const char* const kMediaHostnameKeywords[] = {
    "CHROMECAST", "APPLETV", "APPLE-TV", "HOMEPOD", "ROKU", "SONOS",
    "PLEX", "FIRETV", "FIRE-TV", "SHIELD", "SYNOLOGY", "QNAP",
    "SMART-TV", "SMARTTV", "BRAVIA",
};
const char* const kComputerHostnameKeywords[] = {
    "DESKTOP-", "LAPTOP-", "WIN-", "MACBOOK", "IMAC", "UBUNTU", "DEBIAN",
};
const char* const kPrinterHostnameKeywords[] = {
    "PRINTER", "-PRINT", "HPLASERJET", "OFFICEJET",
};
const char* const kMobileHostnameKeywords[] = {
    "IPHONE", "IPAD", "ANDROID", "GALAXY-", "PIXEL-",
};
// Game consoles land here rather than a dedicated category - "consumer
// electronics appliance, not a general-purpose computer" is the same
// spirit as the rest of this bucket, and a fifth DeviceClass isn't
// worth it for three keywords.
const char* const kIotHostnameKeywords[] = {
    "SONOFF", "SHELLY", "TASMOTA", "ESP-", "ESP_", "TUYA", "WEMO",
    "ECOBEE", "ARLO-", "PLAYSTATION", "XBOX", "NINTENDO-SWITCH",
};

bool matchesAny(const String& upper, const char* const* keywords, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (upper.indexOf(keywords[i]) >= 0) return true;
    }
    return false;
}

template <size_t N>
constexpr size_t countOf(const char* const (&)[N]) {
    return N;
}

}  // namespace

void DeviceClassifier::classify(HostInfo& host, bool isGateway) {
    if (isGateway) {
        host.deviceClass = DeviceClass::Router;
        return;
    }

    if (host.vendor.length()) {
        String v = host.vendor;
        v.toUpperCase();

        if (matchesAny(v, kRouterKeywords, countOf(kRouterKeywords))) {
            host.deviceClass = DeviceClass::Router;
        } else if (matchesAny(v, kMediaKeywords, countOf(kMediaKeywords))) {
            host.deviceClass = DeviceClass::MediaServer;
        } else if (matchesAny(v, kIotKeywords, countOf(kIotKeywords))) {
            host.deviceClass = DeviceClass::IoT;
        } else if (matchesAny(v, kMobileKeywords, countOf(kMobileKeywords))) {
            host.deviceClass = DeviceClass::Mobile;
        } else if (matchesAny(v, kPrinterKeywords, countOf(kPrinterKeywords))) {
            host.deviceClass = DeviceClass::Printer;
        } else if (matchesAny(v, kComputerKeywords, countOf(kComputerKeywords))) {
            host.deviceClass = DeviceClass::Computer;
        }
    }

    // Hostname only gets a vote when the vendor pass above left the host
    // Unknown, or landed on Mobile/Computer - the two categories whose
    // OUI blocks are genuinely reused across unrelated product lines
    // (an "Apple" OUI covers iPhones, MacBooks, Apple TVs and HomePods
    // alike; a "Microsoft" OUI covers Surface laptops and Xbox consoles).
    // A Router/MediaServer/IoT/Printer vendor match is never
    // second-guessed here - that OUI keyword match is a stronger, more
    // specific signal than a hostname the device or its user chose.
    bool refinable = host.deviceClass == DeviceClass::Unknown || host.deviceClass == DeviceClass::Mobile ||
                      host.deviceClass == DeviceClass::Computer;
    if (host.hostname.length() && refinable) {
        String h = host.hostname;
        h.toUpperCase();

        if (matchesAny(h, kMediaHostnameKeywords, countOf(kMediaHostnameKeywords))) {
            host.deviceClass = DeviceClass::MediaServer;
        } else if (matchesAny(h, kComputerHostnameKeywords, countOf(kComputerHostnameKeywords))) {
            host.deviceClass = DeviceClass::Computer;
        } else if (matchesAny(h, kPrinterHostnameKeywords, countOf(kPrinterHostnameKeywords))) {
            host.deviceClass = DeviceClass::Printer;
        } else if (matchesAny(h, kMobileHostnameKeywords, countOf(kMobileHostnameKeywords))) {
            host.deviceClass = DeviceClass::Mobile;
        } else if (matchesAny(h, kIotHostnameKeywords, countOf(kIotHostnameKeywords))) {
            host.deviceClass = DeviceClass::IoT;
        }
    }
}
