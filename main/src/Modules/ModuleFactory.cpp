#include "esp_log.h"

#include "src/Modules/ModuleFactory.hpp"
#include "src/Modules/Ac/Ac.hpp"

static const char *TAG = "MODULE_FACTORY";

std::unique_ptr<IModule> ModuleFactory::create(const ModuleConfig &cfg)
{
    return std::visit([](const auto& concreteConfig) -> std::unique_ptr<IModule> {
        using T = std::decay_t<decltype(concreteConfig)>;

        if constexpr (std::is_same_v<T, AcModuleConfig>) return std::make_unique<Ac>();

        return nullptr;

    }, cfg);
}
