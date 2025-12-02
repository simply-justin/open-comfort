#include "Rs485.hpp"
extern "C" {
#include "driver/uart.h"
#include "esp_log.h"
}

bool Rs485Bus::init(const BusConfig &cfg) {
    uart_config_t uc = {
        .baud_rate = cfg.baud,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE
    };

    uart_param_config(uartPort, &uc);
    uart_set_pin(uartPort, cfg.txPin, cfg.rxPin, -1, -1);
    uart_driver_install(uartPort, 2048, 2048, 0, nullptr, 0);

    return true;
}

void Rs485Bus::write(const uint8_t *data, size_t len) {
    uart_write_bytes(uartPort, (const char*)data, len);
}

int Rs485Bus::read(uint8_t *buffer, size_t maxLen, uint32_t timeoutMs) {
    return uart_read_bytes(uartPort, buffer, maxLen, timeoutMs);
}
