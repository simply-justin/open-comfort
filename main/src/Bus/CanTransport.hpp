#pragma once

#include "src/Bus/BusConfig.hpp"
#include "src/Bus/ICanChannel.hpp"

namespace opencomfort {

class CanTransport final : public ICanChannel {
public:
    explicit CanTransport(CanBusConfig cfg);
    ~CanTransport() override;

    CanTransport(const CanTransport&) = delete;
    CanTransport& operator=(const CanTransport&) = delete;

    BusResult open() override;
    BusResult close() override;
    bool isOpen() const override;

    BusResult send(const CanFrame& frame, TickType_t timeout) override;
    BusResult receive(CanFrame& frame, TickType_t timeout) override;

    const char* name() const override;

private:
    CanBusConfig cfg_;
    bool open_ = false;
};

}  // namespace opencomfort
