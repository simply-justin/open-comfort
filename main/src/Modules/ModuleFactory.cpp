#include "src/Modules/ModuleFactory.hpp"
#include "src/Modules/Ac/Ac.hpp"

#include <type_traits>

extern "C" {
    #include "esp_log.h"
}

static const char* TAG = "MODULE_FACTORY";

/**
 * @brief Create a module instance based on the concrete type in ModuleConfig.
 *
 * Uses std::visit() to detect which configuration type is held by the variant
 * and constructs the corresponding module implementation.
 *
 * @return Unique pointer to a module, or nullptr if unsupported.
 */
std::unique_ptr<IModule> ModuleFactory::create(const ModuleConfig& cfg)
{
    return std::visit([](const auto& concreteConfig) -> std::unique_ptr<IModule>
    {
        using T = std::decay_t<decltype(concreteConfig)>;

        if constexpr (std::is_same_v<T, AcModuleConfig>) return std::make_unique<Ac>(concreteConfig);

        ESP_LOGE(TAG, "Unknown module configuration type");
        return nullptr;

    }, cfg);
}
