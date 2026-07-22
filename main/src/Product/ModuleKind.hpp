#pragma once

#include <cstdint>

namespace opencomfort {

enum class ModuleKind : uint8_t {
    Ac = 1,
};

inline const char* toString(ModuleKind kind)
{
    switch (kind) {
        case ModuleKind::Ac: return "ac";
        default: return "unknown";
    }
}

}  // namespace opencomfort
