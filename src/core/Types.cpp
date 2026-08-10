#include "Types.h"
#include <cstdio>

String macToString(const uint8_t mac[6]) {
    char buf[18];
    snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return String(buf);
}

const char* deviceClassLabel(DeviceClass c) {
    switch (c) {
        case DeviceClass::Router:      return "ROUTER";
        case DeviceClass::IoT:         return "IOT";
        case DeviceClass::Mobile:      return "MOBILE";
        case DeviceClass::Computer:    return "PC";
        case DeviceClass::Printer:     return "PRINTER";
        case DeviceClass::MediaServer: return "MEDIA";
        default:                       return "UNKNOWN";
    }
}
