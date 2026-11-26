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
// Index of the wiring whose SCK the indoor unit clocks. Without a clock it returns the
// wiring found last time (kept in NVS), or 0.
size_t detectWiring(const MhiWiring* wirings, size_t count);
int activeSckPin();
// Clock edges seen on each wiring's SCK during the boot probe (-1 before it ran).
int probedEdges(size_t wiring);
uint32_t observedClockBursts();
esp_err_t exchange(const AcDesiredState* command, AcReportedState& state);
}
