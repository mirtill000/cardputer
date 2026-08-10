#include "DeviceClassifier.h"

namespace {

const char* const kRouterKeywords[] = {
    "NETGEAR", "D-LINK", "MIKROTIK", "UBIQUITI", "CISCO", "ZYXEL",
    "AVM ", "ARRIS", "TECHNICOLOR", "SAGEMCOM", "TP-LINK",
};
const char* const kIotKeywords[] = {
    "ESPRESSIF", "RASPBERRY PI", "ITEAD", "TUYA", "SONOFF", "AMAZON",
    "NEST LABS", "PHILIPS", "SONOS", "ROKU", "RING LLC", "WYZE", "GOOGLE",
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

bool matchesAny(const String& vendorUpper, const char* const* keywords, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (vendorUpper.indexOf(keywords[i]) >= 0) return true;
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
    if (host.vendor.isEmpty()) {
        return;  // no OUI match yet — stays Unknown rather than guessing
    }

    String v = host.vendor;
    v.toUpperCase();

    if (matchesAny(v, kRouterKeywords, countOf(kRouterKeywords))) {
        host.deviceClass = DeviceClass::Router;
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
