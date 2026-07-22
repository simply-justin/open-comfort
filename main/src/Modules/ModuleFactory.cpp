#include "src/Modules/ModuleFactory.hpp"

#include "src/Modules/Ac/Ac.hpp"

extern "C" {
#include "esp_log.h"
}

namespace opencomfort {
namespace {
constexpr const char* kTag = "ModuleFactory";
}

std::unique_ptr<IModule> ModuleFactory::create(const ProductDefinition& product,
                                               const UserConfig& config)
{
    if (product.module != config.module) {
        ESP_LOGE(kTag, "Config module does not match product module");
        return nullptr;
    }

    switch (product.module) {
        case ModuleKind::Ac:
            return std::make_unique<AcModule>(product, config);
        default:
            ESP_LOGE(kTag, "Unsupported module");
            return nullptr;
    }
}

}  // namespace opencomfort
