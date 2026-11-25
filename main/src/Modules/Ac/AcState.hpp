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

constexpr size_t kMaxAcVanes = 8;
constexpr size_t kMaxAcVaneName = 11;

struct AcDesiredState {
    bool powerOn = false;
    AcMode mode = AcMode::Cool;
    float targetTempC = 22.0f;
    AcFanSpeed fan = AcFanSpeed::Auto;
    uint8_t vanes = 0;  // index into the profile's vane positions; 0 when it has none
};

// What the unit itself reports. Each value is only meaningful when its has* flag is set,
// which depends on what the profile can read back.
struct AcReportedState {
    bool valid = false;  // the unit answered with at least one plausible value
    float returnAirC = 0.0f;
    float setTempC = 0.0f;
    bool hasRoomTemp = false;
    bool hasSetTemp = false;
    bool hasPower = false;
    bool powerOn = false;
    bool hasMode = false;
    AcMode mode = AcMode::Auto;
    bool hasFan = false;
    AcFanSpeed fan = AcFanSpeed::Auto;
    bool hasVanes = false;
    uint8_t vanes = 0;
    bool hasCompressor = false;
    bool compressorRunning = false;
};

enum class AcAction : uint8_t {
    Unknown = 0,
    Off,
    Idle,
    Cooling,
    Heating,
    Drying,
    Fan,
};

// What the unit is doing right now, from read-back values (desired values fill gaps).
// Unknown when the profile cannot read the compressor.
inline AcAction acAction(const AcDesiredState& desired, const AcReportedState& reported)
{
    if (!reported.hasCompressor) return AcAction::Unknown;
    const bool on = reported.hasPower ? reported.powerOn : desired.powerOn;
    const AcMode mode = reported.hasMode ? reported.mode : desired.mode;
    if (!on || mode == AcMode::Off) return AcAction::Off;
    if (!reported.compressorRunning) return mode == AcMode::Fan ? AcAction::Fan : AcAction::Idle;
    switch (mode) {
        case AcMode::Cool: return AcAction::Cooling;
        case AcMode::Heat: return AcAction::Heating;
        case AcMode::Dry: return AcAction::Drying;
        case AcMode::Fan: return AcAction::Fan;
        case AcMode::Auto:
        default:
            // Auto runs the compressor either way; the room temperature tells which.
            if (reported.hasRoomTemp) {
                const float target = reported.hasSetTemp ? reported.setTempC : desired.targetTempC;
                return reported.returnAirC > target ? AcAction::Cooling : AcAction::Heating;
            }
            return AcAction::Unknown;
    }
}

inline const char* toString(AcAction action)
{
    switch (action) {
        case AcAction::Off: return "off";
        case AcAction::Idle: return "idle";
        case AcAction::Cooling: return "cooling";
        case AcAction::Heating: return "heating";
        case AcAction::Drying: return "drying";
        case AcAction::Fan: return "fan";
        case AcAction::Unknown:
        default: return "unknown";
    }
}

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
    Vanes,
    Literal,
};

// What a polled register reports, and how its reply byte is decoded.
enum class AcReport : uint8_t {
    None = 0,
    RoomTemp,
    SetTemp,
    Power,
    Mode,
    Fan,
    Vanes,
    Compressor,
};

constexpr size_t kMaxAcReportMap = 12;

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
    AcReport report = AcReport::None;
    uint8_t replyByte = 0;   // which reply byte holds the value
    uint8_t mask = 0xFF;     // applied before decoding
    float scale = 1.0f;      // temperatures: value * scale + offset
    float offset = 0.0f;
    // Enums (mode, fan, vanes): raw value -> AcMode / AcFanSpeed / vane index.
    uint8_t mapRaw[kMaxAcReportMap] = {};
    uint8_t mapValue[kMaxAcReportMap] = {};
    size_t mapCount = 0;
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
    char vaneNames[kMaxAcVanes][kMaxAcVaneName + 1] = {};
    uint8_t vaneCodes[kMaxAcVanes] = {};
    size_t vaneCount = 0;
    bool loaded = false;
};

}  // namespace opencomfort
