/*
OpenComfort Firmware
© 2025 [COMPANY]
[WEBSITE]

Licensed under GPLv3 for personal, educational, and non-commercial use.

Commercial use including embedding this firmware in products or services
requires a commercial license from [COMPANY].

Contact: [EMAIL]

Contributors agree that submitted code may be used under both
GPLv3 and commercial licenses.
*/

extern "C" {
    #include "esp_log.h"
    #include "esp_spiffs.h"
    #include "nvs_flash.h"
}

#include <memory>
#include "src/Models/DeviceConfig.hpp"
#include "src/Modules/IModule.hpp"
#include "src/Modules/ModuleFactory.hpp"

static const char *TAG = "APP_MAIN";

extern "C" void app_main(void)
{
    // Check if we have an existing device profile.
    auto cfg = std::make_unique<DeviceConfig>(); // TEMP

    // If we dont have a profile yet we need to init the captive portal.
    if (!cfg) {
        ESP_LOGI(TAG, "No device profile found, starting captive portal...");
        return; // TEMP
    }

    // If we do have a profile we need to init the main application logic.
    // First we need to determine the module to load based on the profile.
    std::unique_ptr<IModule> module = ModuleFactory::create(cfg->moduleConfig);

    // Check if the module exits and is correctly booted.
    if (!module) {
        ESP_LOGE(TAG, "Failed to create module instance, halting.");
        return;
    }

    module->bootstrap();
    if (!module->isBooted()) {
        ESP_LOGE(TAG, "Module failed to boot, halting.");
        return;
    }

    ESP_LOGI(TAG, "Entering main loop.");

    // Start the main application loop.
    while (true) {
        module->run();
    }
}