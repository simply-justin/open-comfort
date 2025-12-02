#pragma once

#include <cstddef>
#include <cstdint>

enum class BusType {
    UART,
    SPI,
    I2C,
    IR,
    RS485,
    CAN
};

struct BusConfig {
    BusType type;

    int txPin = -1;
    int rxPin = -1;

    int sckPin = -1;
    int mosiPin = -1;
    int misoPin = -1;

    int sdaPin = -1;
    int sclPin = -1;

    int baud = 0;
    int freq = 0;

    int address = 0;   // I2C
    int channel = 0;   // IR
};

class IBus {
public:
    virtual ~IBus() = default;

    virtual bool init(const BusConfig &cfg) = 0;

    virtual void write(const uint8_t *data, size_t len) = 0;
    virtual int  read(uint8_t *buffer, size_t maxLen, uint32_t timeoutMs) = 0;
};