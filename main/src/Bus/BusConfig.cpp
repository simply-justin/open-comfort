#include "src/Bus/BusConfig.hpp"

namespace opencomfort {
namespace {

bool isValidGpio(gpio_num_t pin, bool allowNc = false)
{
    if (pin == GPIO_NUM_NC) {
        return allowNc;
    }
    return pin >= 0 && pin < GPIO_NUM_MAX;
}

}  // namespace

BusStatus validate(const UartBusConfig& cfg)
{
    if (cfg.port < UART_NUM_0 || cfg.port >= UART_NUM_MAX) {
        return BusStatus::InvalidPort;
    }
    if (!isValidGpio(cfg.txPin) || !isValidGpio(cfg.rxPin)) {
        return BusStatus::InvalidPin;
    }
    if (cfg.rs485 && !isValidGpio(cfg.dePin, true)) {
        return BusStatus::InvalidPin;
    }
    if (cfg.baudRate < 300 || cfg.baudRate > 3000000) {
        return BusStatus::InvalidBaud;
    }
    return BusStatus::Ok;
}

BusStatus validate(const SpiBusConfig& cfg)
{
    if (cfg.host < SPI1_HOST || cfg.host > SPI3_HOST) {
        return BusStatus::InvalidPort;
    }
    if (!isValidGpio(cfg.sckPin) || !isValidGpio(cfg.mosiPin) ||
        !isValidGpio(cfg.misoPin, true) || !isValidGpio(cfg.csPin, true)) {
        return BusStatus::InvalidPin;
    }
    if (cfg.clockHz < 10000 || cfg.clockHz > 40000000) {
        return BusStatus::InvalidBaud;
    }
    if (cfg.mode > 3 || cfg.maxTransfer == 0 || cfg.maxTransfer > 4096) {
        return BusStatus::InvalidArgument;
    }
    return BusStatus::Ok;
}

BusStatus validate(const CanBusConfig& cfg)
{
    if (!isValidGpio(cfg.txPin) || !isValidGpio(cfg.rxPin)) {
        return BusStatus::InvalidPin;
    }
    if (cfg.baudRate != 125000 && cfg.baudRate != 250000 && cfg.baudRate != 500000) {
        return BusStatus::InvalidBaud;
    }
    return BusStatus::Ok;
}

}  // namespace opencomfort
