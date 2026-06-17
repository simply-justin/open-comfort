#pragma once

#include <variant>
#include <cstdint>

/**
 * @file ModuleConfig.hpp
 * @brief Configuration structures for high-level device modules.
 *
 * Each module type (AC, Fan, Purifier, etc.) defines its own configuration
 * structure. ModuleConfig is a variant holding exactly one of them, and it
 * is consumed by ModuleFactory to construct the correct module.
 */

/**
 * @struct AcModuleConfig
 * @brief Configuration for the AC module.
 *
 * Currently empty, but intended to hold profile IDs, hardware parameters,
 * or other AC-specific configuration in future versions.
 */
struct AcModuleConfig {};

/**
 * @brief Variant type containing all possible module configurations.
 *
 * The active module is determined by the variant's contained type.
 * ModuleFactory inspects the variant and constructs the corresponding module.
 */
using ModuleConfig = std::variant<AcModuleConfig>;
