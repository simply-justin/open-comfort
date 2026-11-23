#pragma once

#include <string>

#include "src/Configs/ModuleConfig.hpp"

/**
 * @file DeviceConfig.hpp
 * @brief Root device configuration structure.
 *
 * Represents the persisted configuration for an OpenComfort device,
 * including user-visible metadata and the selected functional module
 * (AC, purifier, fan, etc.).
 */

/**
 * @struct DeviceConfig
 * @brief Top-level device configuration.
 *
 * This structure is typically loaded from SPIFFS/NVS at boot and passed
 * to ModuleFactory to select and construct the appropriate module.
 */
struct DeviceConfig {
    int configVersion = 1;
    std::string friendlyName = "OpenComfort Device";
    bool isValid = false;

    // Wi-Fi station credentials for connecting after setup.
    std::string wifiSsid;
    std::string wifiPassword;

    std::string selectedProductId;

    ModuleConfig moduleConfig;
};
