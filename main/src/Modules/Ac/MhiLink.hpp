#pragma once

#include "src/Product/ProductDefinition.hpp"
#include "src/Modules/Ac/AcState.hpp"

namespace opencomfort::mhi {
// The bundled MHI CNS protocol uses short or extended frames clocked by the indoor unit.
constexpr size_t frameSize = 20;
constexpr size_t extendedFrameSize = 33;
bool decode(const uint8_t* frame, size_t length, AcReportedState& state);
void encode(uint8_t* frame, const AcDesiredState* command, bool second, size_t length = frameSize);
esp_err_t start(const BoardSpiPins& pins, bool extended);
uint32_t observedClockBursts();
esp_err_t exchange(const AcDesiredState* command, AcReportedState& state);
}
