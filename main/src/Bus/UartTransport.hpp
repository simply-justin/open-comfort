#pragma once

#include "src/Bus/BusConfig.hpp"
#include "src/Bus/IByteStream.hpp"

namespace opencomfort {

class UartTransport final : public IByteStream {
public:
    explicit UartTransport(UartBusConfig cfg);
    ~UartTransport() override;

    UartTransport(const UartTransport&) = delete;
    UartTransport& operator=(const UartTransport&) = delete;

    BusResult open() override;
    BusResult close() override;
    bool isOpen() const override;

    BusResult write(const uint8_t* data, size_t len) override;
    BusResult read(uint8_t* buffer, size_t maxLen, TickType_t timeout) override;

    const char* name() const override;

private:
    UartBusConfig cfg_;
    bool open_ = false;
};

}  // namespace opencomfort
