#include "Uart.hpp"
extern "C" {
#include "driver/uart.h"
#include "esp_log.h"
}

static const char *TAG = "UartBus";

UartBus::UartBus(int port) : uartPort(port) {}

bool UartBus::init(const BusConfig &cfg) {
    uart_config_t uc = {
        .baud_rate = cfg.baud,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT
    };

    ESP_ERROR_CHECK(uart_param_config(uartPort, &uc));
    ESP_ERROR_CHECK(uart_set_pin(uartPort, cfg.txPin, cfg.rxPin,
                                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    ESP_ERROR_CHECK(uart_driver_install(uartPort, 2048, 0, 0, nullptr, 0));

    ESP_LOGI(TAG, "UART init on TX=%d RX=%d Baud=%d", cfg.txPin, cfg.rxPin, cfg.baud);
    return true;
}

void UartBus::write(const uint8_t *data, size_t len) {
    uart_write_bytes(uartPort, (const char*)data, len);
}

int UartBus::read(uint8_t *buffer, size_t maxLen, uint32_t timeoutMs) {
    int n = uart_read_bytes(uartPort, buffer, maxLen, timeoutMs / portTICK_PERIOD_MS);
    return n < 0 ? 0 : n;
}
