#pragma once

#include <string>
#include <vector>
#include <map>

#include "AC/Enums/Mode.hpp"
#include "AC/Enums/FanSpeed.hpp"

/**
 * @brief UART communication configuration for AC profiles.
 *
 * This structure defines the serial communication parameters used by
 * certain AC protocols (e.g., UART or RS485). All fields are optional
 * and may rely on defaults unless explicitly configured.
 */
struct UartConfig {
    /**
     * @brief Baud rate for serial communication.
     *
     * A value of `0` means the baud rate is not specified and must be
     * determined by the protocol or device profile.
     */
    int baud = 0; // 0 = not specified

    /**
     * @brief Number of data bits per UART frame.
     *
     * Typically 7 or 8, depending on protocol requirements.
     */
    int dataBits = 8;

    /**
     * @brief Parity configuration.
     *
     * Expected values may include `"none"`, `"even"`, or `"odd"`.
     */
    std::string parity = "none";

    /**
     * @brief Number of stop bits.
     *
     * Common values are `1` or `2`.
     */
    int stopBits = 1;
};

struct Profile {
    std::string profileId;
    std::string brand;
    std::vector<std::string> modelNumbers;

    std::string protocol; // "UART", "RS485", "IR"
    UartConfig uartConfig;
    int minTemp = 16;
    int maxTemp = 30;
    std::vector<Mode> supportedModes;
    std::vector<FanSpeed> supportedFanSpeeds;

    std::vector<std::string> features;
    std::map<std::string, std::vector<std::string>> commandTemplates;
};
