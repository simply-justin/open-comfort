#pragma once

/**
 * @file FanControl.hpp
 * @brief Horizontal and vertical fan direction options for AC control.
 */

/**
 * @enum FanControlHorizontal
 * @brief Represents horizontal airflow directions for an AC unit.
 *
 * Values correspond to typical vane positions or modes.
 */
enum class FanControlHorizontal {
    Left,         ///< Airflow directed fully to the left
    Left_Center,  ///< Between left and center
    Center,       ///< Straight ahead
    Right_Center, ///< Between center and right
    Right,        ///< Fully to the right
    Wide,         ///< Wide spread airflow mode
    Spot,         ///< Narrow, focused airflow
    Swing         ///< Automatic horizontal sweeping
};

/**
 * @enum FanControlVertical
 * @brief Represents vertical airflow directions for an AC unit.
 *
 * Values correspond to vane tilt positions or sweeping mode.
 */
enum class FanControlVertical {
    Up,          ///< Airflow fully upward
    Up_Center,   ///< Between up and center
    Down_Center, ///< Between center and down
    Down,        ///< Airflow fully downward
    Swing        ///< Automatic vertical sweeping
};
