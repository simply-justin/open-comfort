#include "src/Modules/Ac/Ac.hpp"

extern "C" {
    #include "esp_log.h"
}

static const char* TAG = "MODULE_AC";

/**
 * @brief Perform AC module initialization.
 *
 * Responsible for preparing the AC control system:
 *   - loading the AC profile,
 *   - initializing the bus,
 *   - creating frame/command engines,
 *   - validating configuration.
 *
 * In this stub implementation, boot succeeds immediately.
 */
void Ac::bootstrap() {
    ESP_LOGI(TAG, "Booting AC module...");

    // TODO: Load ACProfile from storage or embedded assets
    // TODO: Initialize bus (IBus instance via BusFactory)
    // TODO: Create and initialize FrameEngine / CommandEngine
    // TODO: Validate profile + bus + protocol compatibility
    // TODO: Set initial AC state and prepare runtime logic

    booted_ = true;

    ESP_LOGI(TAG, "AC module booted");
}

/**
 * @brief Main execution loop for the AC module.
 *
 * Executed repeatedly by the application. This function should be
 * non-blocking or cooperate with FreeRTOS scheduling.
 *
 * Typical responsibilities:
 *   - polling the bus for new frames,
 *   - updating AC state,
 *   - processing user or schedule commands,
 *   - sending protocol frames when needed.
 */
void Ac::run() {
    if (!booted_) {
        ESP_LOGW(TAG, "run() called before module boot");
        return;
    }

    // TODO: Periodic AC logic (FrameEngine->sendAndReceive(), decode frames, etc.)
    // TODO: Handle time-based state updates or scheduled commands
    // TODO: Push updates to device state or report telemetry
}
