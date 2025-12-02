#pragma once

enum class FanSpeed {
    /**
     * @brief Automatic fan speed regulation.
     *
     * The system determines the appropriate fan speed dynamically.
     */
    Auto = 0,

    /**
     * @brief Low fan speed.
     */
    Low  = 1,

    /**
     * @brief Medium fan speed.
     */
    Med  = 2,

    /**
     * @brief High fan speed.
     */
    High = 3
};
