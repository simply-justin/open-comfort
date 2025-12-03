#pragma once

#include <variant>
#include <cstdint>

/**
 * @file Bus.hpp
 * @brief Configuration structures for all supported bus types.
 */

/**
 * @struct UartConfig
 * @brief UART / RS485 bus configuration.
 */
struct UartConfig {
    int txPin = -1;        ///< UART TX pin
    int rxPin = -1;        ///< UART RX pin
    int baudRate = 9600;   ///< Baud rate for UART or RS485
};

/**
 * @struct SpiConfig
 * @brief SPI bus configuration parameters.
 */
struct SpiConfig {
    int sckPin  = -1;      ///< SPI clock pin
    int mosiPin = -1;      ///< SPI MOSI pin
    int misoPin = -1;      ///< SPI MISO pin
    int freqHz  = 1000000; ///< SPI clock frequency in Hz
};

/**
 * @struct CanConfig
 * @brief CAN bus configuration parameters.
 */
struct CanConfig {
    int txPin = -1;        ///< CAN TX pin
    int rxPin = -1;        ///< CAN RX pin
    int baudRate = 500000; ///< CAN bus baud rate
};

/**
 * @brief Variant holding the configuration for any supported bus type.
 *
 * This allows BusFactory (and other systems) to receive a single
 * configuration object whose actual type depends on BusType.
 */
using Bus = std::variant<UartConfig, SpiConfig, CanConfig>;
