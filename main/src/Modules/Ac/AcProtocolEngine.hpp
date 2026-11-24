#pragma once

#include "esp_err.h"

#include "src/Bus/BusFactory.hpp"
#include "src/Modules/Ac/AcState.hpp"

namespace opencomfort {

class AcProtocolEngine {
public:
    esp_err_t loadFromJson(const char* jsonText);
    bool isLoaded() const;

    esp_err_t applyDesiredState(BusHandle& bus, const AcDesiredState& desired);
    esp_err_t poll(BusHandle& bus, AcReportedState& reported);

    const AcProtocolProfile& profile() const;

private:
    AcProtocolProfile profile_{};
    uint8_t workingFrame_[kMaxAcFrameBytes] = {};

    const AcCommandDef* findCommand(const char* name) const;
    uint8_t placeholderMask(AcPlaceholder placeholder) const;
    uint8_t encodePlaceholder(AcPlaceholder placeholder, const AcDesiredState& desired) const;
    esp_err_t applyCommand(const char* name, const AcDesiredState& desired);
    BusResult transmitFrame(BusHandle& bus, const uint8_t* data, size_t len);
    BusResult exchange(BusHandle& bus, const uint8_t* tx, size_t txLen, uint8_t* rx, size_t rxLen);
};

}  // namespace opencomfort
