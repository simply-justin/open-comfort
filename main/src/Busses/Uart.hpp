#pragma once

#include "IBus.hpp"

/**
 * @file Uart.hpp
 * @brief UART-based implementation of a command transport interface.
 */

/**
 * @class Uart
 * @brief UART transport implementation using an ESP-IDF UART peripheral.
 *
 * Provides simple blocking read/write operations based on UartConfig.
 * The caller must invoke init() before performing transfers.
 */
class Uart : public IBus {
public:
    /**
     * @brief Construct a UART transport from the given configuration.
     * @param c UART configuration parameters.
     */
    explicit Uart(const UartConfig& c);

    /**
     * @brief Initialize the UART hardware with the stored configuration.
     * @return true on successful initialization, false otherwise.
     */
    bool init();

    /**
     * @brief Write raw bytes to the UART bus.
     *
     * Implementations should return false if transmission fails.
     *
     * @param data Pointer to the data buffer.
     * @param len Number of bytes to send.
     * @return true on success, false on failure.
     */
    bool write(const uint8_t* data, size_t len);

    /**
     * @brief Read bytes from the UART bus.
     *
     * Performs a blocking read up to @p maxLen bytes.
     * Returns 0 if the timeout expires or an error occurs.
     *
     * @param buffer Destination buffer.
     * @param maxLen Maximum number of bytes to read.
     * @return Number of bytes read.
     */
    int read(uint8_t* buffer, size_t maxLen);

private:
    UartConfig cfg; ///< UART configuration parameters.
};
