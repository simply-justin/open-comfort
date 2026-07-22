#pragma once

#include <cstdint>

#include "freertos/FreeRTOS.h"

#include "src/Bus/BusStatus.hpp"

namespace opencomfort {

struct CanFrame {
    uint32_t id = 0;
    bool extended = false;
    uint8_t dlc = 0;
    uint8_t data[8] = {};
};

class ICanChannel {
public:
    virtual ~ICanChannel() = default;

    virtual BusResult open() = 0;
    virtual BusResult close() = 0;
    virtual bool isOpen() const = 0;

    virtual BusResult send(const CanFrame& frame, TickType_t timeout) = 0;
    virtual BusResult receive(CanFrame& frame, TickType_t timeout) = 0;

    virtual const char* name() const = 0;
};

}  // namespace opencomfort
