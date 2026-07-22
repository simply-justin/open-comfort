#pragma once

#include <cstdint>
#include <cstring>

namespace opencomfort {

enum class BusKind : uint8_t {
    Unknown = 0,
    Uart = 1,
    Rs485 = 2,
    Spi = 3,
    Can = 4,
};

inline const char* toString(BusKind kind)
{
    switch (kind) {
        case BusKind::Uart: return "uart";
        case BusKind::Rs485: return "rs485";
        case BusKind::Spi: return "spi";
        case BusKind::Can: return "can";
        default: return "unknown";
    }
}

inline BusKind busKindFromString(const char* value)
{
    if (value == nullptr) {
        return BusKind::Unknown;
    }
    if (std::strcmp(value, "uart") == 0) {
        return BusKind::Uart;
    }
    if (std::strcmp(value, "rs485") == 0) {
        return BusKind::Rs485;
    }
    if (std::strcmp(value, "spi") == 0) {
        return BusKind::Spi;
    }
    if (std::strcmp(value, "can") == 0) {
        return BusKind::Can;
    }
    return BusKind::Unknown;
}

}  // namespace opencomfort
