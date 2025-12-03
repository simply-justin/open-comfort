#pragma once

#include "IBus.hpp"

extern "C" {
    #include "driver/twai.h"
}

/**
 * @file Can.hpp
 * @brief CAN bus implementation of the IBus interface using ESP-IDF TWAI.
 */

/**
 * @class Can
 * @brief Implements CAN (TWAI) communication for AC bus transport.
 *
 * Provides basic initialization, frame transmission, and reception.
 * Uses standard 11-bit identifiers and blocking operations.
 *
 * @ingroup buses
 */
class Can : public IBus {
public:
    /**
     * @brief Construct a CAN transport from the given configuration.
     * @param c CAN configuration parameters (pins, baud rate, CAN ID).
     */
    explicit Can(const CanConfig& c);

    /**
     * @brief Initialize the CAN bus.
     *
     * Installs and starts the TWAI driver using the configuration
     * supplied in the constructor.
     *
     * @return true on successful initialization, false on failure.
     */
    bool init() override;

    /**
     * @brief Send a CAN data frame.
     *
     * Transmits up to 8 bytes of payload using the configured CAN ID.
     *
     * @param data Pointer to the payload buffer.
     * @param len  Number of bytes to send (max 8).
     * @return true on success, false on transmission failure.
     */
    bool write(const uint8_t* data, size_t len) override;

    /**
     * @brief Read a CAN data frame.
     *
     * Performs a blocking receive with a short internal timeout.
     * Copies the payload into @p buffer.
     *
     * @param buffer Destination buffer.
     * @param maxLen Maximum number of bytes to copy.
     * @return Number of bytes received, or 0 on timeout/error.
     */
    int read(uint8_t* buffer, size_t maxLen) override;

private:
    CanConfig cfg; ///< CAN configuration parameters.
};
