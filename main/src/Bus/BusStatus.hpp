#pragma once

#include <cstddef>
#include <cstdint>

namespace opencomfort {

enum class BusStatus : uint8_t {
    Ok = 0,
    InvalidArgument,
    InvalidPin,
    InvalidBaud,
    InvalidPort,
    Unsupported,
    DriverInstallFailed,
    AlreadyOpen,
    NotOpen,
    Timeout,
    IoError,
    BufferTooSmall,
    Busy,
};

inline const char* toString(BusStatus status)
{
    switch (status) {
        case BusStatus::Ok: return "ok";
        case BusStatus::InvalidArgument: return "invalid_argument";
        case BusStatus::InvalidPin: return "invalid_pin";
        case BusStatus::InvalidBaud: return "invalid_baud";
        case BusStatus::InvalidPort: return "invalid_port";
        case BusStatus::Unsupported: return "unsupported";
        case BusStatus::DriverInstallFailed: return "driver_install_failed";
        case BusStatus::AlreadyOpen: return "already_open";
        case BusStatus::NotOpen: return "not_open";
        case BusStatus::Timeout: return "timeout";
        case BusStatus::IoError: return "io_error";
        case BusStatus::BufferTooSmall: return "buffer_too_small";
        case BusStatus::Busy: return "busy";
        default: return "unknown";
    }
}

struct BusResult {
    BusStatus status = BusStatus::Ok;
    size_t count = 0;

    explicit operator bool() const
    {
        return status == BusStatus::Ok;
    }

    static BusResult ok(size_t bytes = 0)
    {
        return BusResult{BusStatus::Ok, bytes};
    }

    static BusResult fail(BusStatus status)
    {
        return BusResult{status, 0};
    }
};

}  // namespace opencomfort
