#pragma once

#include <memory>
#include <string>

#include "src/Models/ModuleConfig.hpp"
#include "src/Modules/IModule.hpp"
#include "src/Modules/Ac/Ac.hpp"

/**
 * @brief Factory that creates modules based on configuration.
 */
class ModuleFactory {
public:
    /**
     * @brief Creates the appropriate module instance.
     *
     * @param cfg ModuleConfig loaded from configuration storage.
     * @return std::unique_ptr<IModule> instance or nullptr if not found.
     */
    static std::unique_ptr<IModule> create(const ModuleConfig &cfg);
};
