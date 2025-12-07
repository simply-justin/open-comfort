#pragma once

#include <string>
#include "src/Models/ModuleConfig.hpp"

struct DeviceConfig {
    std::string friendlyName = "OpenComfort Device";
    ModuleConfig moduleConfig;
};