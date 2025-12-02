#pragma once
#include "IBus.hpp"

class Rs485Bus : public IBus {
public:
    bool init(const BusConfig &cfg) override;
    void write(const uint8_t *data, size_t len) override;
    int  read(uint8_t *buffer, size_t maxLen, uint32_t timeoutMs) override;

private:
    int uartPort = UART_NUM_2;
};
