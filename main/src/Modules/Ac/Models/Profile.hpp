#pragma once

#include <string>
#include <vector>
#include <map>

#include "src/Modules/Ac/Enums/Mode.hpp"
#include "src/Modules/Ac/Enums/FanSpeed.hpp"

/**
 * @file Profile.hpp
 * @brief Defines the structure of an AC control profile and its capabilities.
 */
/**
 * @struct Profile
 * @brief Describes a complete AC protocol profile, including capabilities,
 *        supported features, and command mappings.
 *
 * Profiles are typically loaded from JSON and used to translate high-level
 * commands (power, mode, temperature) into the protocol-specific command
 * sequences required by a particular AC brand/model.
 */
struct Profile {
    std::string profileId;                      ///< Unique identifier for the profile
    std::string brand;                          ///< Brand name (e.g., "Daikin", "Midea")
    std::vector<std::string> modelNumbers;      ///< List of associated model numbers

    BusType protocol_bus;                       ///< Communication protocol ("UART", "RS485", "IR")
    Bus bus;                                    ///< UART/RS485 settings (if applicable)

    int minTemp = 16;                           ///< Minimum allowed setpoint temperature
    int maxTemp = 30;                           ///< Maximum allowed setpoint temperature

    std::vector<Mode> supportedModes;           ///< Supported AC operating modes
    std::vector<FanSpeed> supportedFanSpeeds;   ///< Supported discrete fan speeds

    std::vector<std::string> features;          ///< Optional feature flags (e.g., "3DAuto", "Defrost")

    /**
     * @brief Root command definitions shared by all models/families under this profile.
     *
     * Keys represent logical command names (e.g., "PowerOn", "TempUp").
     * Values represent protocol-specific command sequences as strings.
     */
    std::map<std::string, std::vector<std::string>> commandTemplates;
};
