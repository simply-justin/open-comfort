#pragma once

#include <memory>

#include "Models/Profile.hpp"
#include "Enums/BusType.hpp"
#include "IBus.hpp"
#include "Uart.hpp"
#include "Spi.hpp"
#include "Can.hpp"

/**
 * @file BusFactory.hpp
 * @brief Factory for creating IBus implementations based on BusConfig.
 */

/**
 * @class BusFactory
 * @brief Creates the appropriate IBus subclass depending on BusConfig::type.
 *
 * The caller receives a std::unique_ptr<IBus>, ensuring ownership and
 * automatic cleanup. The selected bus instance is not initialized—call
 * init() with the same BusConfig after creation.
 *
 * @ingroup buses
 */
class BusFactory {
public:
    /**
     * @brief Create an IBus instance based on the requested bus type.
     *
     * The returned bus instance is not initialized; the caller must still
     * call IBus::init(profile).
     *
     * @param profile Contains the desired profile and other configuration options.
     * @return std::unique_ptr<IBus> The created bus instance, or nullptr on invalid type.
     */
    static std::unique_ptr<IBus> create(const Profile &profile) {
        switch (profile.protocol_bus) {
            case BusType::UART:  return std::make_unique<Uart>();
            case BusType::RS485: return std::make_unique<Uart>();
            case BusType::SPI:   return std::make_unique<Spi>();
            case BusType::CAN:   return std::make_unique<Can>();
        }

        return nullptr;
    }
};