#include "src/Application/Application.hpp"

#include <memory>

#include "sdkconfig.h"

#include "src/Config/FactoryReset.hpp"
#include "src/Config/UserConfigStore.hpp"
#include "src/Connectors/ConnectorManager.hpp"
#include "src/Connectors/Matter/MatterConnector.hpp"
#include "src/Connectors/Mqtt/HomeAssistantMqttConnector.hpp"
#include "src/Connectors/Web/RuntimeHttpConnector.hpp"
#include "src/Http/CaptivePortal.hpp"
#include "src/Modules/ModuleFactory.hpp"
#include "src/Product/ProductDefinition.hpp"
#include "src/Profiles/ProfileRepository.hpp"
#include "src/Runtime/RuntimeController.hpp"
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

    if (product.board != nullptr) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(FactoryReset::init(product.board->factoryResetPin));
        // Hold BOOT/reset pin at power-on to wipe config and re-enter portal.
        if (FactoryReset::checkAndPerform(CONFIG_OPENCOMFORT_FACTORY_RESET_HOLD_MS)) {
            return;
        }
    }

    ConfigLoadResult loaded = UserConfigStore::load();
    UserConfig config = loaded.config;
    if (loaded.status == ConfigStatus::MigrationRequired &&
        UserConfigStore::validate(config, product) == ConfigStatus::Ok) {
        loaded.status = UserConfigStore::save(config);
        if (loaded.status == ConfigStatus::Ok) {
            ESP_LOGI(kTag, "Migrated user configuration to schema %u",
                     static_cast<unsigned>(kUserConfigSchemaVersion));
        }
    }
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
    } else {
        (void)ProfileRepository::refreshRemoteCatalog(product);
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

    RuntimeController runtime(*module);
    if (!runtime.init()) {
        ESP_LOGE(kTag, "Runtime controller init failed");
        return;
    }

    ConnectorManager connectors;
    if (!connectors.add(std::make_unique<RuntimeHttpConnector>(runtime, product)) ||
        !connectors.add(std::make_unique<HomeAssistantMqttConnector>(runtime)) ||
        !connectors.add(std::make_unique<MatterConnector>(runtime))) {
        ESP_LOGE(kTag, "Connector registration failed");
        return;
    }
    err = connectors.start(config);
    if (err != ESP_OK) {
        ESP_LOGW(kTag, "One or more connectors started degraded: %s", esp_err_to_name(err));
    }

    ESP_LOGI(kTag, "Entering module run loop");
    TickType_t nextResetPoll = xTaskGetTickCount();
    const uint32_t resetHoldMs = CONFIG_OPENCOMFORT_FACTORY_RESET_HOLD_MS;
    while (true) {
        runtime.process();
        connectors.tick();
        const TickType_t now = xTaskGetTickCount();
        if (now >= nextResetPoll) {
            nextResetPoll = now + pdMS_TO_TICKS(1000);
            if (FactoryReset::checkAndPerform(resetHoldMs)) {
                return;
            }
        }
    }
}

}  // namespace opencomfort
