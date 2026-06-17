#pragma once

#include <memory>
#include <string>

#include "src/Configs/ModuleConfig.hpp"
#include "src/Modules/IModule.hpp"
#include "src/Modules/Ac/Ac.hpp"

/**
 * @file ModuleFactory.hpp
 * @brief Factory responsible for constructing module instances.
 */

/**
 * @class ModuleFactory
 * @brief Creates concrete module implementations based on configuration.
 *
 * The returned module is not initialized; callers must invoke init()
 * before using the module.
 */
class ModuleFactory {
public:
    /**
     * @brief Create a module instance from a ModuleConfig definition.
     *
     * The factory inspects the ModuleConfig (e.g., module type, bus type,
     * or feature flags) and constructs the corresponding IModule subclass.
     *
     * @param cfg Parsed module configuration.
     * @return Unique pointer to the created module, or nullptr if the type is unknown.
     */
    static std::unique_ptr<IModule> create(const ModuleConfig& cfg);
};
