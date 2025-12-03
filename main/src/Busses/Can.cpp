#include "Can.hpp"

extern "C" {
    #include "esp_log.h"
}

static const char* TAG = "Can";

Can::Can(const CanConfig& c)
    : cfg(c)
{}

/**
 * @brief Initialize the CAN (TWAI) controller.
 *
 * Configures the bus with the desired pins and baud rate, installs
 * the TWAI driver, and starts it. A filter is applied to accept all
 * CAN frames.
 */
bool Can::init() {
    // Pin configuration and normal operating mode
    twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(
        cfg.txPin,
        cfg.rxPin,
        TWAI_MODE_NORMAL
    );

    // Select timing configuration based on baud rate
    twai_timing_config_t t_config = TWAI_TIMING_CONFIG_500KBITS();
    switch (cfg.baudRate) {
        case 500000: t_config = TWAI_TIMING_CONFIG_500KBITS(); break;
        case 250000: t_config = TWAI_TIMING_CONFIG_250KBITS(); break;
        case 125000: t_config = TWAI_TIMING_CONFIG_125KBITS(); break;
        default:
            ESP_LOGW(TAG, "Unsupported baud rate %d, defaulting to 500k", cfg.baudRate);
            break;
    }

    // Accept all received frames
    twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

    // Install TWAI driver
    if (twai_driver_install(&g_config, &t_config, &f_config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to install TWAI driver");
        return false;
    }

    // Start the controller
    if (twai_start() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start TWAI driver");
        return false;
    }

    ESP_LOGI(TAG, "CAN initialized TX=%d RX=%d Baud=%d",
             cfg.txPin, cfg.rxPin, cfg.baudRate);

    return true;
}

/**
 * @brief Transmit a CAN frame with up to 8 bytes of payload.
 *
 * Builds a TWAI data frame using the ID defined in the configuration.
 */
bool Can::write(const uint8_t* data, size_t len) {
    if (len > 8) {
        ESP_LOGE(TAG, "Attempted to send %u bytes (max 8)", (unsigned)len);
        return false;
    }

    twai_message_t msg = {};
    msg.identifier = cfg.id;
    msg.data_length_code = len;
    msg.extd = 0; // standard 11-bit ID
    msg.rtr = 0;  // data frame

    memcpy(msg.data, data, len);

    esp_err_t err = twai_transmit(&msg, pdMS_TO_TICKS(10));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "CAN TX failed: %s", esp_err_to_name(err));
        return false;
    }

    return true;
}

/**
 * @brief Receive a CAN frame and copy its payload to @p buffer.
 *
 * A short timeout is used internally. If no frame is available or
 * an error occurs, the function returns 0.
 */
int Can::read(uint8_t* buffer, size_t maxLen) {
    twai_message_t msg = {};

    esp_err_t err = twai_receive(&msg, pdMS_TO_TICKS(10));
    if (err != ESP_OK)
        return 0;

    if (msg.data_length_code > maxLen)
        return 0;

    memcpy(buffer, msg.data, msg.data_length_code);
    return msg.data_length_code;
}
