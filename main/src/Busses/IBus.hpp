#pragma once

#include <cstddef>
#include <cstdint>

#include "Models/Bus.hpp"

/**
 * @file IBus.hpp
 * @brief Abstract interface for all communication bus implementations.
 */

/**
 * @class IBus
 * @brief Common interface for UART, SPI, I²C, IR, RS485, and CAN transports.
 *
 * Provides basic initialization and blocking read/write operations.
 * Each concrete bus implementation applies its own configuration,
 * typically supplied through its constructor.
 */
class IBus {
public:
    virtual ~IBus() = default;

    /**
     * @brief Initialize the bus hardware.
     * @return true on successful setup, false on failure.
     */
    virtual bool init() = 0;

    /**
     * @brief Write raw bytes to the bus.
     *
     * Implementations should return false if the write fails
     * or if the underlying driver reports an error.
     *
     * @param data Pointer to the data buffer.
     * @param len Number of bytes to write.
     * @return true on success, false on failure.
     */
    virtual bool write(const uint8_t* data, size_t len) = 0;

    /**
     * @brief Read raw bytes from the bus.
     *
     * Implementations should block until data is available
     * or a timeout (if defined internally) occurs.
     *
     * @param buffer Destination buffer.
     * @param maxLen Maximum bytes to read.
     * @return Number of bytes read (0 if no data or on error).
     */
    virtual int read(uint8_t* buffer, size_t maxLen) = 0;
};
