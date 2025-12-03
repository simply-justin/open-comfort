#pragma once

#include "IBus.hpp"

extern "C" {
    #include "driver/spi_master.h"
}

/**
 * @file Spi.hpp
 * @brief SPI-based implementation of the IBus communication interface.
 */

/**
 * @class Spi
 * @brief SPI transport implementation using an ESP-IDF SPI peripheral.
 *
 * Uses an SPI master device to perform blocking read/write transfers.
 * The exact SPI mode, frequency, and device configuration are taken
 * from the provided SpiConfig.
 *
 * @ingroup buses
 */
class Spi : public IBus {
public:
    /**
     * @brief Create an SPI transport using the given configuration.
     *
     * The configuration determines pins, clock speed, mode, and host.
     * No hardware is initialized until init() is called.
     */
    explicit Spi(const SpiConfig& c);

    /**
     * @brief Initialize the SPI bus and attach a device.
     *
     * Typical implementations configure the bus, allocate a device handle,
     * and prepare for synchronous transfers.
     *
     * @return true on success, false if initialization fails.
     */
    bool init() override;

    /**
     * @brief Write raw bytes to the SPI bus.
     *
     * Performs a blocking transmit. If the device is full-duplex,
     * incoming data is discarded.
     *
     * @param data Pointer to data buffer.
     * @param len Number of bytes to send.
     * @return true on success, false on failure.
     */
    bool write(const uint8_t* data, size_t len) override;

    /**
     * @brief Read bytes from the SPI bus.
     *
     * Depending on hardware mode:
     * - Full-duplex transactions send dummy bytes to clock data in.
     * - Half-duplex mode may support RX-only transactions.
     *
     * @param buffer Destination buffer.
     * @param maxLen Maximum number of bytes to read.
     * @return Number of bytes read.
     */
    int read(uint8_t* buffer, size_t maxLen) override;

private:
    SpiConfig cfg;                          ///< SPI configuration parameters.
    spi_device_handle_t handle = nullptr;   ///< Attached SPI device handle.
};
