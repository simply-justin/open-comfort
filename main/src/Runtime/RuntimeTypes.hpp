#pragma once

#include <cstdint>

#include "esp_err.h"

#include "src/Modules/Ac/AcState.hpp"
#include "src/Product/ModuleKind.hpp"

namespace opencomfort {

constexpr size_t kRuntimeCommandQueueLength = 8;

enum class RuntimeCommandSource : uint8_t {
    Internal = 0,
    LocalWeb,
    HomeAssistantMqtt,
    Matter,
    Schedule,
};

enum class RuntimeRequestStatus : uint8_t {
    Accepted = 0,
    QueueFull,
    NotReady,
    InvalidArgument,
};

struct RuntimeCommand {
    uint32_t requestId = 0;
    RuntimeCommandSource source = RuntimeCommandSource::Internal;
    AcDesiredState desired{};
};

struct AcRuntimeCapabilities {
    int minTempC = 16;
    int maxTempC = 30;
    bool supportsCooling = true;
    bool supportsHeating = true;
    bool supportsDry = true;
    bool supportsFan = true;
    bool supportsAuto = true;
    // Vane positions from the profile, empty when the unit has none.
    char vaneNames[kMaxAcVanes][kMaxAcVaneName + 1] = {};
    size_t vaneCount = 0;
    bool reportsAction = false;  // the profile reads the compressor
};

struct RuntimeSnapshot {
    ModuleKind module = ModuleKind::Ac;
    bool available = false;
    uint32_t revision = 0;
    uint32_t lastRequestId = 0;
    RuntimeCommandSource lastCommandSource = RuntimeCommandSource::Internal;
    esp_err_t lastApplyError = ESP_OK;
    AcDesiredState desired{};
    AcReportedState reported{};
    AcRuntimeCapabilities acCapabilities{};
};

inline const char* toString(RuntimeRequestStatus status)
{
    switch (status) {
        case RuntimeRequestStatus::Accepted: return "accepted";
        case RuntimeRequestStatus::QueueFull: return "queue_full";
        case RuntimeRequestStatus::NotReady: return "not_ready";
        case RuntimeRequestStatus::InvalidArgument: return "invalid_argument";
        default: return "unknown";
    }
}

}  // namespace opencomfort
