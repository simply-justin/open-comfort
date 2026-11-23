#pragma once

#include <variant>
#include <cstdint>
#include <string>

#include "src/Models/Bus.hpp"

/**
 * @file ModuleConfig.hpp
 * @brief Configuration structures for high-level device modules.
 *
 * Each module type (AC, Fan, Purifier, etc.) defines its own configuration
 * structure. ModuleConfig is a variant holding exactly one of them, and it
 * is consumed by ModuleFactory to construct the correct module.
 */

enum class ModuleType {
    Ac
};

struct RemoteProfileMetadata {
    std::string manifestUrl;
    std::string profileUrl;
    std::string profileVersion;
    std::string checksum;
};

/**
 * @struct AcModuleConfig
 * @brief Configuration for the AC module.
 */
struct AcModuleConfig {
    ModuleType type = ModuleType::Ac;
    std::string productId = "mitsubishi_heavy_industries";
    std::string acProfilePath = "/spiffs/profiles/mitsubishi_heavy_industries.json";
    std::string acFriendlyName = "Air Conditioner";
    Bus bus = SpiConfig{};
    RemoteProfileMetadata remote;
};

/**
 * @brief Variant type containing all possible module configurations.
 *
 * The active module is determined by the variant's contained type.
 * ModuleFactory inspects the variant and constructs the corresponding module.
 */
using ModuleConfig = std::variant<AcModuleConfig>;
