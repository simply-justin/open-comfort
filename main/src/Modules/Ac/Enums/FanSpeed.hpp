#pragma once

/**
 * @file FanSpeed.hpp
 * @brief Fan speed levels for AC units.
 */

/**
 * @enum FanSpeed
 * @brief Represents discrete fan speed settings.
 *
 * Numerical values map directly to protocol-level speed codes.
 */
enum class FanSpeed {
    Auto = 0, ///< Automatic fan speed regulation
    Low  = 1, ///< Low speed
    Med  = 2, ///< Medium speed
    High = 3  ///< High speed
};
