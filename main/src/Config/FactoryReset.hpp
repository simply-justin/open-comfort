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
};

}  // namespace opencomfort
