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
constexpr size_t kMaxMqttHostLen = 127;
constexpr size_t kMaxMqttDiscoveryPrefixLen = 32;
constexpr uint16_t kUserConfigSchemaVersion = 2;

struct AcUserConfig {
    char profileId[kMaxProfileIdLen + 1] = {};
    char profileVersion[kMaxProfileVersionLen + 1] = {};
    char modelId[kMaxModelIdLen + 1] = {};
    BusKind bus = BusKind::Unknown;
};

// Frozen schema used only to migrate devices shipped with schema v1.
struct UserConfigV1 {
    uint16_t schemaVersion = 1;
    bool provisioned = false;
    char friendlyName[kMaxFriendlyNameLen + 1] = "OpenComfort";
    ModuleKind module = ModuleKind::Ac;
    AcUserConfig ac = {};
};

struct ConnectorConfig {
    bool localWebEnabled = true;
    bool mqttEnabled = false;
    bool matterEnabled = false;
    bool mqttTls = true;
    uint16_t mqttPort = 8883;
    char mqttHost[kMaxMqttHostLen + 1] = {};
    char mqttDiscoveryPrefix[kMaxMqttDiscoveryPrefixLen + 1] = "homeassistant";
};

struct UserConfig {
    uint16_t schemaVersion = kUserConfigSchemaVersion;
    bool provisioned = false;
    char friendlyName[kMaxFriendlyNameLen + 1] = "OpenComfort";
    ModuleKind module = ModuleKind::Ac;
    AcUserConfig ac = {};
    ConnectorConfig connectors = {};
};

static_assert(sizeof(UserConfig) > sizeof(UserConfigV1), "v2 config must extend v1");

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
