#include "I2c.hpp"
extern "C" {
#include "driver/i2c.h"
#include "esp_log.h"
}

bool I2cBus::init(const BusConfig &cfg) {
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = cfg.sdaPin,
        .scl_io_num = cfg.sclPin,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master = {.clk_speed = cfg.freq}
    };

    ESP_ERROR_CHECK(i2c_param_config(I2C_NUM_0, &conf));
    ESP_ERROR_CHECK(i2c_driver_install(I2C_NUM_0, conf.mode, 0, 0, 0));
    return true;
}

void I2cBus::write(const uint8_t *data, size_t len) {
    // Typically needs slave address -> implement when needed
}

int I2cBus::read(uint8_t *buffer, size_t maxLen, uint32_t timeoutMs) {
    return 0;
}
