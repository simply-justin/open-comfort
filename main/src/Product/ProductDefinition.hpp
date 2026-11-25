#pragma once

#include <cstddef>
#include <cstdint>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "driver/uart.h"

#include "src/Bus/BusKind.hpp"
#include "src/Product/ModuleKind.hpp"

namespace opencomfort {

struct BoardUartPins {
    uart_port_t port = UART_NUM_1;
    gpio_num_t txPin = GPIO_NUM_NC;
    gpio_num_t rxPin = GPIO_NUM_NC;
    gpio_num_t dePin = GPIO_NUM_NC;
};

struct BoardSpiPins {
    spi_host_device_t host = SPI2_HOST;
    gpio_num_t sckPin = GPIO_NUM_NC;
    gpio_num_t mosiPin = GPIO_NUM_NC;
    gpio_num_t misoPin = GPIO_NUM_NC;
    gpio_num_t csPin = GPIO_NUM_NC;
};

struct BoardCanPins {
    gpio_num_t txPin = GPIO_NUM_NC;
    gpio_num_t rxPin = GPIO_NUM_NC;
};

struct BoardDefinition {
    const char* revision;
    BoardUartPins uart;
    BoardSpiPins spi;
    BoardSpiPins mhi;
    bool mhiExtendedFrames;
    BoardCanPins can;
    gpio_num_t factoryResetPin;
    int maxSpiClockHz;
    int maxUartBaud;
};

struct ProductDefinition {
    const char* productId;
    ModuleKind module;
    const BoardDefinition* board;
    const BusKind* allowedBuses;
    size_t allowedBusCount;
    const char* portalApPassword;
    const char* profileCatalogUrl;
    bool supportsMatter;
    bool supportsMqtt;
};

const ProductDefinition& currentProduct();
bool productAllowsBus(const ProductDefinition& product, BusKind kind);

}  // namespace opencomfort
