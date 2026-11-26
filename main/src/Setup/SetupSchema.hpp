#pragma once

#include <cstddef>
#include <cstdint>

namespace opencomfort {

// Sized to what modules use (AC: one step, three fields). Each field holds every option,
// so these multiply quickly: 1 x 4 fields is ~15 KB, built on the heap during setup.
constexpr size_t kMaxSetupSteps = 1;
constexpr size_t kMaxSetupFields = 4;
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
