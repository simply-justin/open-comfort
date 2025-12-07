#pragma once

#include "src/Modulces/Ac/Enums/Mode.hpp"
#include "src/Modulces/Ac/Enums/FanSpeed.hpp"
#include "src/Modulces/Ac/Enums/FanControl.hpp"

/**
 * @file State.hpp
 * @brief Represents the complete runtime state of an AC unit.
 */

/**
 * @struct State
 * @brief Holds all user-controllable and system-reported AC state values.
 *
 * This structure is typically used for building protocol frames,
 * interpreting feedback, or tracking desired vs. reported states.
 */
struct State {
    bool powerOn = false;                                                   ///< Whether the AC is powered on
    bool defrost = false;                                                   ///< Indicates defrost mode (if supported)
    bool ThreeDAuto = false;                                                ///< Automatic 3D airflow mode (vendor-specific)

    float currentTemp = 22.0f;                                              ///< Measured or reported ambient temperature
    float targetTemp  = 22.0f;                                              ///< User-selected temperature setpoint

    Mode mode = Mode::Off;                                                  ///< Operating mode (cool, heat, auto, etc.)
    FanSpeed fanSpeed = FanSpeed::Auto;                                     ///< Fan speed level

    FanControlHorizontal fanHorizontal = FanControlHorizontal::Center;      ///< Horizontal airflow direction
    FanControlVertical   fanVertical   = FanControlVertical::Down_Center;   ///< Vertical airflow direction
};
