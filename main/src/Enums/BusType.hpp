#pragma once

/**
 * @file BusType.hpp
 * @brief Enumeration of supported hardware communication bus types.
 *
 * Used throughout the system (e.g., BusFactory, ModuleConfig) to select
 * the appropriate bus implementation.
 */

/**
 * @enum BusType
 * @brief Identifies the type of communication bus to use.
 *
 * UNKNOWN should not appear during normal operation and typically
 * indicates an invalid or uninitialized configuration.
 */
enum class BusType {
    UNKNOWN,
    SPI,
    UART,
    RS485,
    CAN
};
