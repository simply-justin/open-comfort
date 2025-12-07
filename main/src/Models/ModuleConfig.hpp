#pragma once

#include <variant>
#include <cstdint>

struct AcModuleConfig {
};

using ModuleConfig = std::variant<AcModuleConfig>;