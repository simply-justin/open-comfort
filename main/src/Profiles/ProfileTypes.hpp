#pragma once

#include <cstddef>
#include <cstdint>

#include "src/Bus/BusKind.hpp"

namespace opencomfort {

constexpr size_t kMaxCatalogEntries = 32;
constexpr size_t kMaxModelsPerProfile = 16;
constexpr size_t kMaxMakeNameLen = 64;
constexpr size_t kMaxUrlLen = 160;

struct ProfileCatalogEntry {
    char id[49] = {};
    char makeId[49] = {};
    char makeName[kMaxMakeNameLen + 1] = {};
    char models[kMaxModelsPerProfile][49] = {};
    size_t modelCount = 0;
    BusKind bus = BusKind::Unknown;
    char profileVersion[17] = {};
    char relativePath[97] = {};
    bool bundled = true;
};

struct ProfileCatalog {
    uint16_t schemaVersion = 1;
    ProfileCatalogEntry entries[kMaxCatalogEntries] = {};
    size_t count = 0;
};

struct LoadedAcProfile {
    char profileId[49] = {};
    char brand[65] = {};
    BusKind bus = BusKind::Unknown;
    int baudOrClockHz = 0;
    uint8_t spiMode = 0;
    char profileVersion[17] = {};
    bool valid = false;
};

}  // namespace opencomfort
