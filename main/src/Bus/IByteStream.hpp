#pragma once

#include <cstddef>
#include <cstdint>

#include "freertos/FreeRTOS.h"

#include "src/Bus/BusStatus.hpp"

namespace opencomfort {

class IByteStream {
public:
    virtual ~IByteStream() = default;

    virtual BusResult open() = 0;
    virtual BusResult close() = 0;
    virtual bool isOpen() const = 0;

    virtual BusResult write(const uint8_t* data, size_t len) = 0;
    virtual BusResult read(uint8_t* buffer, size_t maxLen, TickType_t timeout) = 0;

    virtual const char* name() const = 0;
};

}  // namespace opencomfort
