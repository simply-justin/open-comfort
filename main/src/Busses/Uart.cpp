#include "Uart.hpp"

extern "C" {
    #include "driver/uart.h"
    #include "esp_log.h"
}

static const char *TAG = "Uart";

Uart::Uart(const UartConfig& c)
    : cfg(c)
{}

/**
 * @brief Initialize UART hardware using cfg.
 */
bool Uart::init() {
    uart_config_t uc = {
        .baud_rate = cfg.baudRate,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT
    };

    // Configure UART parameters
    if (uart_param_config(cfg.port, &uc) != ESP_OK)
        return false;

    // Assign TX/RX pins
    if (uart_set_pin(cfg.port, cfg.txPin, cfg.rxPin,
                     UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE) != ESP_OK)
        return false;

    // Install driver (RX buffer = 2048 bytes, no TX buffer, no event queue)
    if (uart_driver_install(cfg.port, 2048, 0, 0, nullptr, 0) != ESP_OK)
        return false;

    ESP_LOGI(TAG,
             "UART initialized (port=%d, TX=%d, RX=%d, Baud=%d)",
             cfg.port, cfg.txPin, cfg.rxPin, cfg.baudRate);

    return true;
}

/**
 * @brief Write len bytes to UART.
 */
bool Uart::write(const uint8_t *data, size_t len) {
    // uart_write_bytes returns number of bytes queued or <0 on error
    int written = uart_write_bytes(cfg.port, reinterpret_cast<const char*>(data), len);
    return (written == static_cast<int>(len));
}

/**
 * @brief Blocking read with fixed timeout.
 *
 * Returns 0 on timeout or error.
 */
int Uart::read(uint8_t *buffer, size_t maxLen) {
    const int timeoutTicks = pdMS_TO_TICKS(10); // fixed timeout (10ms)

    int received = uart_read_bytes(cfg.port, buffer, maxLen, timeoutTicks);
    return (received < 0) ? 0 : received;
}
