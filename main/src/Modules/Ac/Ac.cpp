#include "esp_log.h"
#include "src/Modules/Ac/Ac.hpp"

static const char *TAG = "MODULE_AC";

void Ac::bootstrap() {
    ESP_LOGI(TAG, "Booting AC module...");

    // TODO: Load ACProfile, setup bus, create FrameEngine, etc.

    booted_ = true;
    ESP_LOGI(TAG, "AC module booted");
}

void Ac::run() {
    if (!booted_) return;

    // TODO: Add AC logic (FrameEngine->sendAndReceive(), protocol decode, etc.)
}
