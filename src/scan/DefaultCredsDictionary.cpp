#include "DefaultCredsDictionary.h"

namespace DefaultCredsDictionary {

const DefaultCredential kEntries[] = {
    {"admin", "admin"},
    {"admin", "password"},
    {"admin", ""},
    {"admin", "1234"},
    {"root", "root"},
    {"root", "admin"},
    {"root", ""},
    {"user", "user"},
};

const size_t kCount = sizeof(kEntries) / sizeof(kEntries[0]);

}  // namespace DefaultCredsDictionary
