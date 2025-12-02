#pragma once

#include <memory>

#include "IBus.hpp"
#include "Uart.hpp"
#include "Spi.hpp"
#include "I2c.hpp"
#include "Ir.hpp"
#include "Rs485.hpp"
#include "Can.hpp"

class BusFactory {
public:
    static std::unique_ptr<IBus> create(const BusConfig &cfg) {
        switch (cfg.type) {
            case BusType::UART:  return std::make_unique<UartBus>();
            case BusType::SPI:   return std::make_unique<SpiBus>();
            case BusType::I2C:   return std::make_unique<I2cBus>();
            case BusType::IR:    return std::make_unique<IrBus>();
            case BusType::RS485: return std::make_unique<Rs485Bus>();
            case BusType::CAN:   return std::make_unique<CanBus>();
        }
        return nullptr;
    }
};
