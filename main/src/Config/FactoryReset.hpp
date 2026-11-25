#pragma once

#include "esp_err.h"

#include "driver/gpio.h"

namespace opencomfort {

class FactoryReset {
public:
    static esp_err_t init(gpio_num_t pin);
    // Returns true if the button was held long enough and reset completed.
    static bool checkAndPerform(uint32_t holdMs = 5000);
    static esp_err_t perform();

    // Counts quick power cycles in NVS and resets on the configured count.
    // Returns true if a reset was started (the device restarts).
    static bool checkPowerCycles();
    // Next counter value: power-on boots add one, any other reset starts over.
    static uint8_t nextPowerCycleCount(uint8_t stored, bool powerOnReset);
};

}  // namespace opencomfort
