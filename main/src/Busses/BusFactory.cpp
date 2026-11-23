#include "src/Busses/BusFactory.hpp"

#include <type_traits>

extern "C" {
    #include "esp_log.h"
}

/**
 * @file BusFactory.cpp
 * @brief Implementation of the BusFactory for creating IBus instances.
 *
 * This file contains the logic that inspects a Bus configuration
 * (std::variant<UartConfig, SpiConfig, CanConfig>) and constructs the
 * appropriate concrete bus implementation. This allows higher-level
 * modules to remain bus-agnostic.
 */

static const char* TAG = "BUS_FACTORY";

/**
 * @brief Create a concrete IBus instance from a Bus configuration.
 *
 * Uses `std::visit` to determine which configuration type is stored
 * in the variant. Based on the active configuration type, the matching
 * bus implementation is constructed:
 *
 *  - UartConfig → Uart
 *  - SpiConfig  → Spi
 *  - CanConfig  → Can
 *
 * The returned bus is *not* initialized. The caller must invoke:
 *
 *     bus->init();
 *
 * before using read/write operations.
 *
 * @param cfg Variant containing the selected bus configuration.
 * @return A unique_ptr to the created IBus implementation, or nullptr on error.
 */
std::unique_ptr<IBus> BusFactory::create(const Bus& cfg)
{
    return std::visit([](const auto& concreteConfig) -> std::unique_ptr<IBus>
    {
        using T = std::decay_t<decltype(concreteConfig)>;

        if constexpr (std::is_same_v<T, SpiConfig>)  return std::make_unique<Spi>(concreteConfig);
        if constexpr (std::is_same_v<T, UartConfig>) return std::make_unique<Uart>(concreteConfig);
        if constexpr (std::is_same_v<T, CanConfig>)  return std::make_unique<Can>(concreteConfig);

        ESP_LOGE(TAG, "Unknown Bus configuration type");
        return nullptr;

    }, cfg);
}
