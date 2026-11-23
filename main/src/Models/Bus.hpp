#pragma once

#include <variant>
#include <cstdint>
#include <type_traits>

/**
 * @file Bus.hpp
 * @brief Configuration structures and type definitions for supported bus types.
 */

/**
 * @struct UartConfig
 * @brief UART / RS485 bus configuration.
 */
struct UartConfig {
    int port = 1;
    int txPin = -1;
    int rxPin = -1;
    int baudRate = 9600;
};

/**
 * @struct SpiConfig
 * @brief SPI bus configuration parameters.
 */
struct SpiConfig {
    int host = 2;
    int sckPin  = -1;
    int mosiPin = -1;
    int misoPin = -1;
    int csPin = -1;
    int mode = 0;
    int frequency = 1000000;
};

/**
 * @struct CanConfig
 * @brief CAN bus configuration parameters.
 */
struct CanConfig {
    int txPin = -1;
    int rxPin = -1;
    int baudRate = 500000;
    uint32_t id = 0x100;
};

/**
 * @brief Variant containing configuration for any supported bus type.
 *
 * A higher-level object (e.g., DeviceConfig) typically stores:
 *   - a BusType indicating which bus is active, and
 *   - a Bus variant containing the matching configuration.
 *
 * Example:
 *     BusType type = BusType::CAN;
 *     Bus cfg = CanConfig{5, 4, 500000};
 *
 * This allows BusFactory to construct the correct IBus implementation.
 */
using Bus = std::variant<UartConfig, SpiConfig, CanConfig>;

template <typename T>
constexpr bool isBusConfig = std::disjunction_v<
    std::is_same<T, UartConfig>,
    std::is_same<T, SpiConfig>,
    std::is_same<T, CanConfig>
>;