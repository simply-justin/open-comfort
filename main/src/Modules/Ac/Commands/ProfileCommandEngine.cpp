#include "src/Modules/Ac/Commands/ProfileCommandEngine.hpp"

extern "C" {
    #include "esp_log.h"
}

#include <cstdlib>

static const char* TAG = "ProfileCommandEngine";

// Every encoder below is paired with a *value-domain mask*: the OR of every
// value that encoder can ever produce. `applyStep`'s `binary_or` path clears
// exactly those bits before OR-ing the new value in, instead of OR-ing the
// value onto whatever was left from a previous command — plain OR can set a
// bit but can never clear one, so e.g. turning power off (`encodePower`
// returning 0x00) used to be a silent no-op once the power-on bit had ever
// been set. The mask is derived purely from the encoder's own output range,
// not from any assumption about unrelated bits (flag bits, other fields)
// elsewhere in the same byte, so it can't clobber anything this encoder
// doesn't itself control.
static constexpr uint8_t kPowerMask = 0x01;
static constexpr uint8_t kModeMask = 0x07;
static constexpr uint8_t kFanMask = 0x03;

static uint8_t encodePower(bool on) {
    return on ? 0x01 : 0x00;
}

static uint8_t encodeMode(Mode mode) {
    switch (mode) {
        case Mode::Cool: return 0x03;
        case Mode::Heat: return 0x04;
        case Mode::Dry: return 0x02;
        case Mode::Fan: return 0x06;
        case Mode::Auto: return 0x00;
        case Mode::Off: return 0x00;
    }
    return 0x00;
}

static uint8_t encodeTemp(float tempC, int minTemp, int maxTemp) {
    int temp = static_cast<int>(tempC);
    if (temp < minTemp) temp = minTemp;
    if (temp > maxTemp) temp = maxTemp;
    return static_cast<uint8_t>((temp - minTemp) * 2);
}

// The widest value `encodeTemp` can produce for this profile's own min/max —
// used to build a mask that covers the full range (see `kPowerMask` et al.
// above), since unlike power/mode/fan the range isn't fixed at compile time.
static uint8_t tempMask(int minTemp, int maxTemp) {
    const int span = maxTemp > minTemp ? maxTemp - minTemp : 0;
    const unsigned maxValue = static_cast<unsigned>(span) * 2;
    unsigned mask = 1;
    while (mask < maxValue) mask = (mask << 1) | 1;
    return static_cast<uint8_t>(mask);
}

static uint8_t encodeFan(FanSpeed fan) {
    switch (fan) {
        case FanSpeed::Auto: return 0x00;
        case FanSpeed::Low: return 0x01;
        case FanSpeed::Med: return 0x02;
        case FanSpeed::High: return 0x03;
    }
    return 0x00;
}

int ProfileCommandEngine::fieldIndex(const std::string& field) {
    if (field.size() < 3 || field[0] != 'D' || field[1] != 'B') return -1;
    char* end = nullptr;
    long index = strtol(field.c_str() + 2, &end, 10);
    if (!end || *end != '\0' || index < 0 || index >= static_cast<long>(kDbFieldCount)) return -1;
    return static_cast<int>(index);
}

bool ProfileCommandEngine::resolveToken(
    const std::string& token,
    const CommandVars& vars,
    const AcProfile& profile,
    uint8_t& out,
    uint8_t& outMask,
    std::string* error
) {
    if (token.empty()) {
        if (error) *error = "empty command token";
        return false;
    }

    if (token[0] == '$') {
        if (token == "$POWER") {
            if (!vars.hasPower) {
                if (error) *error = "missing POWER variable";
                return false;
            }
            out = encodePower(vars.powerOn);
            outMask = kPowerMask;
            return true;
        }
        if (token == "$MODE") {
            if (!vars.hasMode) {
                if (error) *error = "missing MODE variable";
                return false;
            }
            out = encodeMode(vars.mode);
            outMask = kModeMask;
            return true;
        }
        if (token == "$TEMP" || token == "$TROOM") {
            if (!vars.hasTemp) {
                if (error) *error = "missing TEMP variable";
                return false;
            }
            out = encodeTemp(vars.tempC, profile.minTemp, profile.maxTemp);
            outMask = tempMask(profile.minTemp, profile.maxTemp);
            return true;
        }
        if (token == "$FAN") {
            if (!vars.hasFan) {
                if (error) *error = "missing FAN variable";
                return false;
            }
            out = encodeFan(vars.fan);
            outMask = kFanMask;
            return true;
        }

        if (error) *error = "unsupported variable " + token;
        return false;
    }

    char* end = nullptr;
    unsigned long value = strtoul(token.c_str(), &end, 16);
    if (end == token.c_str() || *end != '\0' || value > 0xFF) {
        if (error) *error = "invalid hex token " + token;
        return false;
    }

    // A literal token's "domain" is itself — it's the same constant on
    // every call, so clearing-then-setting it is equivalent to the old
    // plain OR (no behavior change for flag bits like "02"/"20"/"C0").
    out = static_cast<uint8_t>(value);
    outMask = out;
    return true;
}

bool ProfileCommandEngine::applyStep(
    const CommandStep& step,
    const CommandVars& vars,
    const AcProfile& profile,
    std::array<uint8_t, kDbFieldCount>& dbFields,
    std::string* error
) {
    const int index = fieldIndex(step.field);
    if (index < 0) {
        if (error) *error = "invalid field " + step.field;
        return false;
    }

    if (!step.value.empty()) {
        uint8_t value = 0;
        uint8_t unusedMask = 0; // a plain "value" step already fully overwrites the field
        if (!resolveToken(step.value, vars, profile, value, unusedMask, error)) return false;
        (void)unusedMask;
        dbFields[static_cast<size_t>(index)] = value;
        return true;
    }

    for (const auto& token : step.binary_or) {
        uint8_t value = 0;
        uint8_t mask = 0;
        if (!resolveToken(token, vars, profile, value, mask, error)) return false;
        // Clear this token's own bit range before OR-ing its new value in —
        // see the comment above `kPowerMask` for why plain OR can't do this.
        uint8_t& field = dbFields[static_cast<size_t>(index)];
        field = static_cast<uint8_t>((field & ~mask) | value);
    }

    return true;
}

bool ProfileCommandEngine::applyProfileCommand(
    const AcProfile& profile,
    const std::string& commandName,
    const CommandVars& vars,
    std::array<uint8_t, kDbFieldCount>& dbFields,
    std::string* error
) {
    auto it = profile.commands.find(commandName);
    if (it == profile.commands.end()) {
        if (error) *error = "profile command not found: " + commandName;
        return false;
    }

    for (const auto& step : it->second) {
        if (!applyStep(step, vars, profile, dbFields, error)) return false;
    }

    return true;
}

std::vector<uint8_t> ProfileCommandEngine::buildPayload(
    const AcProfile& profile,
    const std::array<uint8_t, kDbFieldCount>& dbFields
) {
    if (!profile.frame.enabled) {
        return std::vector<uint8_t>(dbFields.begin(), dbFields.end());
    }

    const size_t payloadLength = profile.frame.payloadLength;
    if (payloadLength == 0 || payloadLength > kDbFieldCount) {
        ESP_LOGW(TAG, "Invalid frame payload length, sending raw DB fields");
        return std::vector<uint8_t>(dbFields.begin(), dbFields.end());
    }

    std::vector<uint8_t> payload;
    payload.reserve(profile.frame.header.size() + payloadLength + (profile.frame.checksumSum8 ? 1 : 0));
    payload.insert(payload.end(), profile.frame.header.begin(), profile.frame.header.end());
    payload.insert(payload.end(), dbFields.begin(), dbFields.begin() + static_cast<std::ptrdiff_t>(payloadLength));

    if (profile.frame.checksumSum8) {
        uint8_t sum = 0;
        for (uint8_t byte : payload) sum += byte;
        payload.push_back(sum);
    }

    return payload;
}
