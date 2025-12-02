#pragma once
#include "IBus.hpp"

class SpiBus : public IBus {
public:
    bool init(const BusConfig &cfg) override;
    void write(const uint8_t *data, size_t len) override;
    int  read(uint8_t *buffer, size_t maxLen, uint32_t timeoutMs) override;
};
