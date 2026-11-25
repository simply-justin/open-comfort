#pragma once

#include "src/Config/UserConfig.hpp"
#include "src/Modules/Ac/AcState.hpp"
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

UserConfig migrateUserConfigV1(const UserConfigV1& source);
UserConfig migrateUserConfigV2(const UserConfigV2& source);

class UserConfigStore {
public:
    static bool init();
    static ConfigLoadResult load();
    static ConfigStatus save(const UserConfig& config);
    static ConfigStatus clear();
    static ConfigStatus validate(const UserConfig& config, const ProductDefinition& product);

    // Last applied AC state, so a restart restores it instead of resetting the unit.
    // Lives in the user namespace, so a factory reset clears it too.
    static bool loadAcDesired(AcDesiredState& out);
    static ConfigStatus saveAcDesired(const AcDesiredState& state);
};

}  // namespace opencomfort
