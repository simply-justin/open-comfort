#pragma once

#include <optional>
#include "src/Configs/DeviceConfig.hpp"

/**
 * @brief Manages loading and saving the DeviceConfig from SPIFFS.
 *
 * JSON path: "/spiffs/config.json"
 */
class ConfigManager {
public:
    /**
     * @brief Initialize SPIFFS and config system.
     * @return true on success.
     */
    static bool init();

    /**
     * @brief Load device configuration from SPIFFS.
     *
     * @return DeviceConfig
     *         - isValid == false if config missing or parse failed.
     */
    static DeviceConfig load();

    /**
     * @brief Save device configuration to SPIFFS as JSON.
     *
     * @param cfg The configuration to save.
     * @return true on success, false otherwise.
     */
    static bool save(const DeviceConfig& cfg);

private:
    static constexpr const char* kConfigPath = "/spiffs/config.json";
    static bool s_initialized;
};
