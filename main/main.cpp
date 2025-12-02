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

static const char *TAG = "APP_MAIN";

/**
 * 
 */
static void init_spiffs() {
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = nullptr,
        .max_files = 5,
        .format_if_mount_failed = true
    };

    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK) {
        ESP_LOGI(TAG, "Failed to mount or format SPIFFS (%s)", esp_err_to_name(ret));
    } else {
        size_t total = 0, used = 0;
        esp_spiffs_info(nullptr, &total, &used);
        ESP_LOGI(TAG, "SPIFFS mounted: total=%u, used=%u", (unsigned)total, (unsigned)used);
    }
}

extern "C" void app_main(void)
{
    // NVS init
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    init_spiffs();

    auto bus = BusFactory::create(cfg);
bus->init(cfg);

}