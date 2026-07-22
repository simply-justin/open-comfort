#pragma once

#include "src/Config/UserConfig.hpp"
#include "src/Product/ProductDefinition.hpp"

namespace opencomfort {

enum class ConfigStatus : uint8_t {
    Ok = 0,
    NotFound,
    Corrupt,
    Invalid,
    IoError,
    MigrationRequired,
};

inline const char* toString(ConfigStatus status)
{
    switch (status) {
        case ConfigStatus::Ok: return "ok";
        case ConfigStatus::NotFound: return "not_found";
        case ConfigStatus::Corrupt: return "corrupt";
        case ConfigStatus::Invalid: return "invalid";
        case ConfigStatus::IoError: return "io_error";
        case ConfigStatus::MigrationRequired: return "migration_required";
        default: return "unknown";
    }
}

struct ConfigLoadResult {
    ConfigStatus status = ConfigStatus::NotFound;
    UserConfig config = {};
};

class UserConfigStore {
public:
    static bool init();
    static ConfigLoadResult load();
    static ConfigStatus save(const UserConfig& config);
    static ConfigStatus clear();
    static ConfigStatus validate(const UserConfig& config, const ProductDefinition& product);
};

}  // namespace opencomfort
