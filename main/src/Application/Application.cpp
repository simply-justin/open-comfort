#include "src/Application/Application.hpp"

#include <memory>

#include "src/Config/UserConfigStore.hpp"
#include "src/Http/CaptivePortal.hpp"
#include "src/Modules/ModuleFactory.hpp"
#include "src/Product/ProductDefinition.hpp"
#include "src/Profiles/ProfileRepository.hpp"
#include "src/Wifi/WiFiService.hpp"

extern "C" {
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
}

namespace opencomfort {
namespace {
constexpr const char* kTag = "Application";
}

void Application::run()
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    } else {
        ESP_ERROR_CHECK(err);
    }

    if (!UserConfigStore::init()) {
        ESP_LOGE(kTag, "User config store init failed");
        return;
    }

    ESP_ERROR_CHECK(ProfileRepository::initSpiffs());

    const ProductDefinition& product = currentProduct();
    ESP_LOGI(kTag, "Product=%s module=%s board=%s", product.productId, toString(product.module),
             product.board != nullptr ? product.board->revision : "unknown");

    ConfigLoadResult loaded = UserConfigStore::load();
    UserConfig config = loaded.config;
    bool needsProvisioning =
        (loaded.status != ConfigStatus::Ok) ||
        (UserConfigStore::validate(config, product) != ConfigStatus::Ok);

    if (needsProvisioning) {
        ESP_LOGW(kTag, "Provisioning required (config status=%s)", toString(loaded.status));
        CaptivePortal portal(product);
        if (!portal.run(config)) {
            ESP_LOGE(kTag, "Captive portal failed");
            return;
        }
        ESP_LOGI(kTag, "Provisioning complete, restarting");
        esp_restart();
        return;
    }

    ESP_ERROR_CHECK(WiFiService::init());
    err = WiFiService::startStationFromStorage(20000);
    if (err != ESP_OK) {
        ESP_LOGW(kTag, "STA connect failed at boot: %s", esp_err_to_name(err));
    }

    std::unique_ptr<IModule> module = ModuleFactory::create(product, config);
    if (!module) {
        ESP_LOGE(kTag, "Module factory failed");
        return;
    }

    module->bootstrap();
    if (!module->isBooted()) {
        ESP_LOGE(kTag, "Module bootstrap failed");
        return;
    }

    ESP_LOGI(kTag, "Entering module run loop");
    while (true) {
        module->run();
    }
}

}  // namespace opencomfort
