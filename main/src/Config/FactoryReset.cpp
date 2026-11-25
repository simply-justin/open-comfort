#include "src/Config/FactoryReset.hpp"

#include "sdkconfig.h"

#include "src/Config/ConnectorSecretStore.hpp"
#include "src/Config/UserConfigStore.hpp"
#include "src/Connectors/Matter/MatterPlatform.hpp"
#include "src/Wifi/WiFiService.hpp"

extern "C" {
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs.h"
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

// Own namespace: perform() must not erase it before the counter is cleared.
constexpr const char* kBootNamespace = "oc_boot";
constexpr const char* kCycleKey = "cycles";
esp_timer_handle_t s_windowTimer = nullptr;

void storeCycleCount(uint8_t count)
{
    nvs_handle_t handle = 0;
    if (nvs_open(kBootNamespace, NVS_READWRITE, &handle) != ESP_OK) return;
    if (nvs_set_u8(handle, kCycleKey, count) == ESP_OK) {
        (void)nvs_commit(handle);
    }
    nvs_close(handle);
}

void clearCycles(void*)
{
    storeCycleCount(0);
}

}  // namespace

uint8_t FactoryReset::nextPowerCycleCount(uint8_t stored, bool powerOnReset)
{
    if (!powerOnReset) return 0;
    return stored < 255 ? static_cast<uint8_t>(stored + 1) : stored;
}

bool FactoryReset::checkPowerCycles()
{
    const uint32_t threshold = CONFIG_OPENCOMFORT_POWER_CYCLE_RESET_COUNT;
    if (threshold == 0) return false;

    uint8_t stored = 0;
    nvs_handle_t handle = 0;
    if (nvs_open(kBootNamespace, NVS_READONLY, &handle) == ESP_OK) {
        (void)nvs_get_u8(handle, kCycleKey, &stored);
        nvs_close(handle);
    }
    const uint8_t count = nextPowerCycleCount(stored, esp_reset_reason() == ESP_RST_POWERON);
    storeCycleCount(count);

    if (count >= threshold) {
        ESP_LOGW(kTag, "%u quick power cycles", static_cast<unsigned>(count));
        storeCycleCount(0);
        perform();
        return true;
    }
    if (count > 0) {
        ESP_LOGI(kTag, "Quick power cycle %u of %u", static_cast<unsigned>(count),
                 static_cast<unsigned>(threshold));
        // Staying on past the window ends the sequence.
        const esp_timer_create_args_t args = {
            .callback = clearCycles, .arg = nullptr, .dispatch_method = ESP_TIMER_TASK,
            .name = "pwr_cycle", .skip_unhandled_events = true};
        if (s_windowTimer == nullptr && esp_timer_create(&args, &s_windowTimer) != ESP_OK) {
            storeCycleCount(0);  // without the timer a later reboot must not count
            return false;
        }
        (void)esp_timer_start_once(s_windowTimer,
                                   static_cast<uint64_t>(CONFIG_OPENCOMFORT_POWER_CYCLE_WINDOW_MS) * 1000);
    }
    return false;
}

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
    ConnectorSecretStore::clear();
    MatterPlatform::factoryReset();

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
