#include "src/Config/FactoryReset.hpp"

#include "sdkconfig.h"

#include "src/Config/UserConfigStore.hpp"
#include "src/Wifi/WiFiService.hpp"

extern "C" {
#include "esp_log.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
}

namespace opencomfort {
namespace {

constexpr const char* kTag = "FactoryReset";
gpio_num_t s_pin = GPIO_NUM_NC;
bool s_ready = false;

}  // namespace

esp_err_t FactoryReset::init(gpio_num_t pin)
{
    s_pin = pin;
    if (s_pin == GPIO_NUM_NC) {
        s_ready = false;
        return ESP_OK;
    }

    gpio_config_t io = {};
    io.pin_bit_mask = 1ULL << s_pin;
    io.mode = GPIO_MODE_INPUT;
    io.pull_up_en = GPIO_PULLUP_ENABLE;
    io.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io.intr_type = GPIO_INTR_DISABLE;
    const esp_err_t err = gpio_config(&io);
    s_ready = (err == ESP_OK);
    return err;
}

esp_err_t FactoryReset::perform()
{
    ESP_LOGW(kTag, "Performing factory reset");
    UserConfigStore::init();
    UserConfigStore::clear();

    // Best-effort Wi-Fi credential wipe.
    if (WiFiService::init() == ESP_OK) {
        esp_wifi_restore();
    }

    vTaskDelay(pdMS_TO_TICKS(200));
    esp_restart();
    return ESP_OK;
}

bool FactoryReset::checkAndPerform(uint32_t holdMs)
{
    if (!s_ready || s_pin == GPIO_NUM_NC) {
        return false;
    }

    if (gpio_get_level(s_pin) != 0) {
        return false;
    }

    ESP_LOGW(kTag, "Reset button held, waiting %u ms", static_cast<unsigned>(holdMs));
    const TickType_t start = xTaskGetTickCount();
    while ((xTaskGetTickCount() - start) < pdMS_TO_TICKS(holdMs)) {
        if (gpio_get_level(s_pin) != 0) {
            ESP_LOGI(kTag, "Reset cancelled");
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    perform();
    return true;
}

}  // namespace opencomfort
