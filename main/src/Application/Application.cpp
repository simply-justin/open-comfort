#include "src/Application/Application.hpp"

#include <memory>

#include "sdkconfig.h"

#include "src/Config/FactoryReset.hpp"
#include "src/Config/DeviceHostname.hpp"
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
#include "src/Logging/LogBuffer.hpp"
#include "src/Schedule/ScheduleService.hpp"
#include "src/Update/UpdateService.hpp"
#include "src/Wifi/WiFiService.hpp"

extern "C" {
#include "esp_log.h"
#include "esp_ota_ops.h"
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
    LogBuffer::install();  // first, so the portal's log shows the whole boot
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

    // Before anything else uses the configuration: 5 quick power-ons wipe it.
    if (FactoryReset::checkPowerCycles()) {
        return;
    }

    if (product.board != nullptr) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(FactoryReset::init(product.board->factoryResetPin));
        // Hold BOOT/reset pin at power-on to wipe config and re-enter portal.
        if (FactoryReset::checkAndPerform(CONFIG_OPENCOMFORT_FACTORY_RESET_HOLD_MS)) {
            return;
        }
    }

    ConfigLoadResult loaded = UserConfigStore::load();
    UserConfig config = loaded.config;
    // Turn off integrations this build lacks (after switching variant) and, as older
    // firmware allowed both, keep Home Assistant over Matter. Setup is not needed for that.
    if (loaded.status == ConfigStatus::Ok || loaded.status == ConfigStatus::MigrationRequired) {
        ConnectorConfig& c = config.connectors;
        const ConnectorConfig before = c;
        if (c.mqttEnabled && !product.supportsMqtt) c.mqttEnabled = false;
        if (c.matterEnabled && !product.supportsMatter) c.matterEnabled = false;
        if (c.mqttEnabled && c.matterEnabled) c.matterEnabled = false;
        if (loaded.status == ConfigStatus::Ok && (before.mqttEnabled != c.mqttEnabled || before.matterEnabled != c.matterEnabled) &&
            UserConfigStore::save(config) == ConfigStatus::Ok) {
            ESP_LOGW(kTag, "Integration settings adjusted to this firmware variant");
        }
    }
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
        // Reaching setup means this image boots; keep it after an OTA update.
        (void)esp_ota_mark_app_valid_cancel_rollback();
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
    char hostname[kMaxHostnameLen + 1];
    ESP_ERROR_CHECK(resolveDeviceHostname(config, hostname, sizeof(hostname)));
    ESP_ERROR_CHECK_WITHOUT_ABORT(WiFiService::setStationHostname(hostname));
    err = WiFiService::startStationFromStorage(20000);
    if (err != ESP_OK) {
        ESP_LOGW(kTag, "STA connect failed at boot: %s", esp_err_to_name(err));
    } else {
        (void)ProfileRepository::refreshRemoteCatalog(product);
    }
    // SNTP keeps retrying, so this also works when Wi-Fi comes up later.
    ScheduleService::instance().begin();
    UpdateService::instance().begin();

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
    auto web = std::make_unique<RuntimeHttpConnector>(runtime, product);
    web->setConnectorManager(&connectors);
    bool registered = connectors.add(std::move(web));
#if CONFIG_OPENCOMFORT_ENABLE_MQTT
    registered = registered && connectors.add(std::make_unique<HomeAssistantMqttConnector>(runtime));
#endif
    registered = registered && connectors.add(std::make_unique<MatterConnector>(runtime));
    if (!registered) {
        ESP_LOGE(kTag, "Connector registration failed");
        return;
    }
    err = connectors.start(config);
    if (err != ESP_OK) {
        ESP_LOGW(kTag, "One or more connectors started degraded: %s", esp_err_to_name(err));
    }

    // Wi-Fi, module and connectors are up: confirm an OTA image so the bootloader keeps it.
    // An image that crashes before this point is rolled back on the next boot.
    (void)esp_ota_mark_app_valid_cancel_rollback();
    // Main task stack left after boot; warns well before an overflow would.
    const UBaseType_t stackFree = uxTaskGetStackHighWaterMark(nullptr);
    if (stackFree < 1536) {
        ESP_LOGW(kTag, "Main task stack low: %u bytes free", static_cast<unsigned>(stackFree));
    } else {
        ESP_LOGI(kTag, "Main task stack: %u bytes free", static_cast<unsigned>(stackFree));
    }
    ESP_LOGI(kTag, "Entering module run loop");
    TickType_t nextResetPoll = xTaskGetTickCount();
    const uint32_t resetHoldMs = CONFIG_OPENCOMFORT_FACTORY_RESET_HOLD_MS;
    while (true) {
        runtime.process();
        connectors.tick();
        ScheduleService::instance().tick(runtime);
        UpdateService::instance().tick(WiFiService::isStationConnected());
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
