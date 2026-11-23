#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "src/Modules/Ac/Enums/FanSpeed.hpp"
#include "src/Modules/Ac/Enums/Mode.hpp"
#include "src/Modules/Ac/Models/AcProfile.hpp"

struct CommandVars {
    bool hasPower = false;
    bool powerOn = false;
    bool hasMode = false;
    Mode mode = Mode::Off;
    bool hasTemp = false;
    float tempC = 22.0f;
    bool hasFan = false;
    FanSpeed fan = FanSpeed::Auto;
};

class ProfileCommandEngine {
public:
    static constexpr size_t kDbFieldCount = 18;

    static bool applyProfileCommand(
        const AcProfile& profile,
        const std::string& commandName,
        const CommandVars& vars,
        std::array<uint8_t, kDbFieldCount>& dbFields,
        std::string* error = nullptr
    );

    static std::vector<uint8_t> buildPayload(
        const AcProfile& profile,
        const std::array<uint8_t, kDbFieldCount>& dbFields
    );

private:
    static int fieldIndex(const std::string& field);
    static bool resolveToken(
        const std::string& token,
        const CommandVars& vars,
        const AcProfile& profile,
        uint8_t& out,
        uint8_t& outMask,
        std::string* error
    );
    static bool applyStep(
        const CommandStep& step,
        const CommandVars& vars,
        const AcProfile& profile,
        std::array<uint8_t, kDbFieldCount>& dbFields,
        std::string* error
    );
};
