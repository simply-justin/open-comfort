#pragma once

/**
 * @file Mode.hpp
 * @brief Operating modes for an AC unit.
 */

/**
 * @enum Mode
 * @brief Represents the primary operating modes of an air conditioner.
 */
enum class Mode {
    Off,  ///< Device is turned off
    Cool, ///< Cooling mode
    Heat, ///< Heating mode
    Dry,  ///< Dehumidification mode
    Fan,  ///< Fan-only mode (no heating or cooling)
    Auto  ///< Automatic mode based on environment
};
