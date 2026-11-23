/*
OpenComfort Firmware
© 2025 [COMPANY]
[WEBSITE]

Licensed under GPLv3 for personal, educational, and non-commercial use.

Commercial use—including embedding this firmware in products or services—
requires a commercial license from [COMPANY].

Contact: [EMAIL]

Contributors agree that submitted code may be used under both
GPLv3 and commercial licenses.
*/

extern "C" {
    #include "freertos/FreeRTOS.h"
    #include "freertos/task.h"
    #include "esp_log.h"
    #include "nvs_flash.h"
}

#include <memory>

#include "src/Configs/ConfigManager.hpp"
#include "src/Configs/DeviceConfig.hpp"
#include "src/Core/WifiManager.hpp"
#include "src/Http/CaptivePortal.hpp"
#include "src/Http/WebPortal.hpp"
#include "src/Modules/Ac/Ac.hpp"
#include "src/Modules/IModule.hpp"
#include "src/Modules/ModuleFactory.hpp"
#include "src/Protocols/Matter/MatterAdapter.hpp"

static const char* TAG = "APP_MAIN";

static bool initNvs() {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS needs erase (%s), erasing", esp_err_to_name(ret));
        ESP_ERROR_CHECK_WITHOUT_ABORT(nvs_flash_erase());
        ret = nvs_flash_init();
    }

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize NVS: %s", esp_err_to_name(ret));
        return false;
    }
    return true;
}

static DeviceConfig runSetupPortal() {
    ESP_LOGI(TAG, "Starting setup portal");
    CaptivePortal portal;
    return portal.run();
}

extern "C" void app_main(void)
{
    if (!initNvs()) return;

    if (!ConfigManager::init()) {
        ESP_LOGE(TAG, "Unable to initialize SPIFFS/config storage");
        return;
    }

    DeviceConfig cfg = ConfigManager::load();

    if (!cfg.isValid) {
        cfg = runSetupPortal();

        if (!cfg.isValid) {
            ESP_LOGE(TAG, "Setup portal did not produce a valid config");
            return;
        }
    }

    if (!WifiManager::connectStation(cfg.wifiSsid, cfg.wifiPassword, 30000)) {
        ESP_LOGW(TAG, "Wi-Fi connection failed, falling back to setup mode");
        cfg = runSetupPortal();
        if (!cfg.isValid) return;
    }

    std::unique_ptr<IModule> module = ModuleFactory::create(cfg.moduleConfig);
    if (!module) {
        ESP_LOGE(TAG, "Failed to create module");
        return;
    }

    module->bootstrap();
    if (!module->isBooted()) {
        ESP_LOGE(TAG, "Module failed to boot");
        return;
    }

    WebPortal webPortal(cfg, module.get());
    webPortal.start();

    Ac* ac = dynamic_cast<Ac*>(module.get());
    MatterAdapter matter(ac);
    matter.init(false);

    ESP_LOGI(TAG, "Entering main loop");
    while (true) {
        module->run();
        matter.publishState();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
