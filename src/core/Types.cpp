#include "Types.h"

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
