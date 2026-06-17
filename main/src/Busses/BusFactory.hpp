#pragma once

#include <memory>
#include <variant>

#include "src/Enums/BusType.hpp"
#include "src/Models/Bus.hpp"

#include "src/Busses/IBus.hpp"
#include "src/Busses/Uart.hpp"
#include "src/Busses/Spi.hpp"
#include "src/Busses/Can.hpp"

/**
 * @file BusFactory.hpp
 * @brief Factory for constructing IBus instances from a Bus configuration.
 */

/**
 * @class BusFactory
 * @brief Creates a concrete bus implementation based on a Bus variant.
 *
 * The Bus variant holds exactly one of the supported bus configuration
 * structures (UartConfig, SpiConfig, CanConfig). The factory inspects
 * the active variant type and constructs the matching IBus instance.
 *
 * The returned bus is not initialized — callers must invoke bus->init().
 */
class BusFactory {
public:
    /**
     * @brief Create an IBus implementation from a Bus configuration variant.
     *
     * Uses std::visit() to determine which configuration type is contained
     * inside the variant and returns the corresponding bus object.
     *
     * @param bus Variant containing one of the supported bus configurations.
     * @return std::unique_ptr<IBus> The created bus instance, or nullptr if unsupported.
     */
    static std::unique_ptr<IBus> create(const Bus& bus);
};
