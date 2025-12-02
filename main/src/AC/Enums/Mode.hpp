#pragma once

enum class Mode {
    /**
     * @brief Device is turned off.
     */
    Off,

    /**
     * @brief Cooling mode.
     */
    Cool,

    /**
     * @brief Heating mode.
     */
    Heat,

    /**
     * @brief Dry mode.
     */
    Dry,

    /**
     * @brief Fan-only mode.
     */
    Fan,

    /**
     * @brief Automatic mode.
     *
     * The device determines the optimal operating mode based on
     * current environmental conditions.
     */
    Auto
};
