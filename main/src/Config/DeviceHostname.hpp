#pragma once

#include "esp_err.h"
#include "src/Config/UserConfig.hpp"

namespace opencomfort {

// Accepts an optional .local suffix and normalizes ASCII letters to lowercase.
// Empty input resets to the automatic hostname. Other domains/URLs are rejected.
bool normalizeDeviceHostname(const char* input, char* out, size_t size);
bool isValidDeviceHostname(const char* hostname);
esp_err_t resolveDeviceHostname(const UserConfig& config, char* out, size_t size);

}  // namespace opencomfort
