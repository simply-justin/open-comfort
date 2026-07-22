#pragma once

#include <cstddef>
#include <cstdint>

namespace opencomfort {

enum class AcMode : uint8_t {
    Off = 0,
    Cool,
    Heat,
    Dry,
    Fan,
    Auto,
};

enum class AcFanSpeed : uint8_t {
    Auto = 0,
    Low,
    Medium,
    High,
};

struct AcDesiredState {
    bool powerOn = false;
    AcMode mode = AcMode::Cool;
    float targetTempC = 22.0f;
    AcFanSpeed fan = AcFanSpeed::Auto;
};

struct AcReportedState {
    bool valid = false;
    float returnAirC = 0.0f;
    float setTempC = 0.0f;
};

constexpr size_t kMaxAcFrameBytes = 32;
constexpr size_t kMaxAcCommands = 16;
constexpr size_t kMaxAcCommandOps = 8;
constexpr size_t kMaxAcDataRequests = 24;
constexpr size_t kMaxAcCommandName = 24;
constexpr size_t kMaxAcDataName = 24;

enum class AcOpKind : uint8_t {
    SetValue = 0,
    BinaryOr = 1,
};

enum class AcPlaceholder : uint8_t {
    None = 0,
    Power,
    Mode,
    Temp,
    Fan,
    Literal,
};

struct AcCommandOp {
    uint8_t fieldIndex = 0;  // DB0 -> 0
    AcOpKind kind = AcOpKind::SetValue;
    uint8_t literalMask = 0;
    AcPlaceholder placeholder = AcPlaceholder::None;
    uint8_t literalValue = 0;
};

struct AcCommandDef {
    char name[kMaxAcCommandName + 1] = {};
    AcCommandOp ops[kMaxAcCommandOps] = {};
    size_t opCount = 0;
};

struct AcDataRequest {
    char name[kMaxAcDataName + 1] = {};
    uint8_t bytes[8] = {};
    size_t length = 0;
};

struct AcProtocolProfile {
    uint8_t frame[kMaxAcFrameBytes] = {};
    size_t frameSize = 20;
    int minTempC = 16;
    int maxTempC = 30;
    AcCommandDef commands[kMaxAcCommands] = {};
    size_t commandCount = 0;
    AcDataRequest dataRequests[kMaxAcDataRequests] = {};
    size_t dataRequestCount = 0;
    bool loaded = false;
};

}  // namespace opencomfort
