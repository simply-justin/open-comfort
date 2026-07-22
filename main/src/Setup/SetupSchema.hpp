#pragma once

#include <cstddef>
#include <cstdint>

namespace opencomfort {

constexpr size_t kMaxSetupSteps = 8;
constexpr size_t kMaxSetupFields = 12;
constexpr size_t kMaxFieldOptions = 32;

enum class SetupFieldType : uint8_t {
    Text = 0,
    Password,
    Select,
    Number,
};

struct SetupFieldOption {
    char value[49] = {};
    char label[65] = {};
};

struct SetupField {
    char id[33] = {};
    char label[65] = {};
    SetupFieldType type = SetupFieldType::Text;
    bool required = true;
    SetupFieldOption options[kMaxFieldOptions] = {};
    size_t optionCount = 0;
};

struct SetupStep {
    char id[33] = {};
    char title[65] = {};
    char description[129] = {};
    SetupField fields[kMaxSetupFields] = {};
    size_t fieldCount = 0;
};

struct SetupSchema {
    char moduleId[17] = {};
    SetupStep steps[kMaxSetupSteps] = {};
    size_t stepCount = 0;
};

}  // namespace opencomfort
