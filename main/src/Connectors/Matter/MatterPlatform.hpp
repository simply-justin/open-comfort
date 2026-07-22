#pragma once

#include "esp_err.h"

namespace opencomfort {

class RuntimeController;

class MatterPlatform {
public:
    static esp_err_t start(RuntimeController& runtime);
    static void stop();
    static void sync();
    static esp_err_t openCommissioningWindow();
    static esp_err_t factoryReset();
    static bool isRunning();
};

}  // namespace opencomfort
