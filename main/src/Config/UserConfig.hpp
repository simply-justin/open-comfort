#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "src/Bus/BusKind.hpp"
#include "src/Product/ModuleKind.hpp"

namespace opencomfort {

constexpr size_t kMaxFriendlyNameLen = 32;
constexpr size_t kMaxProfileIdLen = 48;
constexpr size_t kMaxModelIdLen = 48;
constexpr size_t kMaxProfileVersionLen = 16;
constexpr uint16_t kUserConfigSchemaVersion = 1;

struct AcUserConfig {
    char profileId[kMaxProfileIdLen + 1] = {};
    char profileVersion[kMaxProfileVersionLen + 1] = {};
    char modelId[kMaxModelIdLen + 1] = {};
    BusKind bus = BusKind::Unknown;
};

struct UserConfig {
    uint16_t schemaVersion = kUserConfigSchemaVersion;
    bool provisioned = false;
    char friendlyName[kMaxFriendlyNameLen + 1] = "OpenComfort";
    ModuleKind module = ModuleKind::Ac;
    AcUserConfig ac = {};
};

inline void copyBounded(char* dest, size_t destSize, const char* src)
{
    if (dest == nullptr || destSize == 0) {
        return;
    }
    if (src == nullptr) {
        dest[0] = '\0';
        return;
    }
    std::strncpy(dest, src, destSize - 1);
    dest[destSize - 1] = '\0';
}

}  // namespace opencomfort
