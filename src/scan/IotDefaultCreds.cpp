#include "IotDefaultCreds.h"
#include <cstddef>  // size_t for kCount - same reason as BleCompanyIds.cpp

namespace IotDefaultCreds {

// Vendor/device-specific documented factory defaults, then a few generics.
// Kept short and well-known on purpose (see header).
const IotCredential kEntries[] = {
    // IP cameras / DVRs
    {"hikvision", "http", "admin", "12345"},
    {"dahua", "http", "admin", "admin"},
    {"axis", "http", "root", "pass"},
    {"foscam", "http", "admin", ""},
    {"vivotek", "http", "root", ""},

    // Routers / networking
    {"tp-link", "http", "admin", "admin"},
    {"tplink", "http", "admin", "admin"},
    {"d-link", "http", "admin", ""},
    {"dlink", "http", "admin", ""},
    {"netgear", "http", "admin", "password"},
    {"linksys", "http", "admin", "admin"},
    {"zyxel", "http", "admin", "1234"},
    {"mikrotik", "http", "admin", ""},
    {"ubiquiti", "http", "ubnt", "ubnt"},
    {"ubiquiti", "telnet", "ubnt", "ubnt"},

    // Printers / other
    {"hewlett", "http", "admin", "admin"},

    // Generic fallbacks (empty keyword = tried on every device).
    {"", "http", "admin", "admin"},
    {"", "http", "admin", ""},
    {"", "http", "admin", "password"},
    {"", "http", "root", "root"},
    {"", "telnet", "admin", "admin"},
    {"", "telnet", "root", "root"},
};

const size_t kCount = sizeof(kEntries) / sizeof(kEntries[0]);

}  // namespace IotDefaultCreds
