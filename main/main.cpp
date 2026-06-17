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
    #include "esp_spiffs.h"
    #include "nvs_flash.h"
}

#include <memory>

#include "src/Config/ConfigManager.hpp"
#include "src/Configs/DeviceConfig.hpp"
#include "src/Modules/IModule.hpp"
#include "src/Modules/ModuleFactory.hpp"

static const char* TAG = "APP_MAIN";

extern "C" void app_main(void)
{
    // Initialize NVS if needed
    nvs_flash_init();

    // Initialize config & SPIFFS
    ConfigManager::init();
    DeviceConfig* cfg = ConfigManager::load();

    if (!cfg.isValid) {
        // Run captive portal for initial setup.
        ESP_LOGI(TAG, "No valid config found, starting captive portal...");

        CaptivePortal portal;
        *cfg = portal.run();

        if (!cfg.isValid) {
            ESP_LOGE(TAG, "Captive portal did not produce a valid config, aborting.");
            return;
        }
    }

    IModule* module = ModuleFactory::create(cfg.moduleConfig);
    if (!module) {
        ESP_LOGE("APP_MAIN", "Failed to create module");
        return;
    }

    module->bootstrap();
    if (!module->isBooted()) {
        ESP_LOGE("APP_MAIN", "Module failed to boot");
        return;
    }

    ESP_LOGI("APP_MAIN", "Entering main loop...");
    while (true) {
        module->run();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
