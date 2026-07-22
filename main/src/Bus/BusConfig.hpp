#pragma once

#include <cstddef>
#include <cstdint>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"

#include "src/Bus/BusKind.hpp"
#include "src/Bus/BusStatus.hpp"

namespace opencomfort {

struct UartBusConfig {
    uart_port_t port = UART_NUM_1;
    gpio_num_t txPin = GPIO_NUM_NC;
    gpio_num_t rxPin = GPIO_NUM_NC;
    gpio_num_t dePin = GPIO_NUM_NC;  // RS485 driver-enable, optional
    int baudRate = 9600;
    TickType_t readTimeout = pdMS_TO_TICKS(50);
    TickType_t writeTimeout = pdMS_TO_TICKS(100);
    bool rs485 = false;
};

struct SpiBusConfig {
    spi_host_device_t host = SPI2_HOST;
    gpio_num_t sckPin = GPIO_NUM_NC;
    gpio_num_t mosiPin = GPIO_NUM_NC;
    gpio_num_t misoPin = GPIO_NUM_NC;
    gpio_num_t csPin = GPIO_NUM_NC;
    int clockHz = 1000000;
    uint8_t mode = 0;
    size_t maxTransfer = 256;
    TickType_t timeout = pdMS_TO_TICKS(100);
};

struct CanBusConfig {
    gpio_num_t txPin = GPIO_NUM_NC;
    gpio_num_t rxPin = GPIO_NUM_NC;
    int baudRate = 500000;
    uint32_t txId = 0;
    uint32_t rxFilterId = 0;
    bool filterEnabled = false;
    TickType_t txTimeout = pdMS_TO_TICKS(100);
    TickType_t rxTimeout = pdMS_TO_TICKS(100);
};

BusStatus validate(const UartBusConfig& cfg);
BusStatus validate(const SpiBusConfig& cfg);
BusStatus validate(const CanBusConfig& cfg);

}  // namespace opencomfort
