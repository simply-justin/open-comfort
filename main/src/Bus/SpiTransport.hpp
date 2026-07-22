#pragma once

#include "driver/spi_master.h"

#include "src/Bus/BusConfig.hpp"
#include "src/Bus/ISpiDevice.hpp"

namespace opencomfort {

class SpiTransport final : public ISpiDevice {
public:
    explicit SpiTransport(SpiBusConfig cfg);
    ~SpiTransport() override;

    SpiTransport(const SpiTransport&) = delete;
    SpiTransport& operator=(const SpiTransport&) = delete;

    BusResult open() override;
    BusResult close() override;
    bool isOpen() const override;

    BusResult transfer(const uint8_t* tx, uint8_t* rx, size_t len, TickType_t timeout) override;
    BusResult write(const uint8_t* data, size_t len, TickType_t timeout) override;
    BusResult read(uint8_t* buffer, size_t maxLen, TickType_t timeout) override;

    const char* name() const override;

private:
    SpiBusConfig cfg_;
    spi_device_handle_t handle_ = nullptr;
    bool busInstalled_ = false;
    bool open_ = false;
};

}  // namespace opencomfort
