#include "src/Config/UserConfigStore.hpp"
#include "src/Config/DeviceHostname.hpp"

#include <cstring>

extern "C" {
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"
}

namespace opencomfort {
UserConfig migrateUserConfigV1(const UserConfigV1& source)
{
    UserConfig out;
    out.schemaVersion = kUserConfigSchemaVersion;
    out.provisioned = source.provisioned;
    copyBounded(out.friendlyName, sizeof(out.friendlyName), source.friendlyName);
    out.module = source.module;
    copyBounded(out.ac.profileId, sizeof(out.ac.profileId), source.ac.profileId);
    copyBounded(out.ac.profileVersion, sizeof(out.ac.profileVersion), source.ac.profileVersion);
    copyBounded(out.ac.modelId, sizeof(out.ac.modelId), source.ac.modelId);
    out.ac.bus = source.ac.bus;
    return out;
}

UserConfig migrateUserConfigV2(const UserConfigV2& source)
{
    UserConfig out;
    out.provisioned = source.provisioned;
    copyBounded(out.friendlyName, sizeof(out.friendlyName), source.friendlyName);
    out.module = source.module;
    out.ac = source.ac;
    out.connectors = source.connectors;
    return out;
}

namespace {

constexpr const char* kTag = "UserConfigStore";
constexpr const char* kNamespace = "oc_user";
constexpr const char* kActiveSlotKey = "active";
constexpr const char* kSlot0Key = "cfg0";
constexpr const char* kSlot1Key = "cfg1";

bool s_initialized = false;

bool ensureNvs()
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(kTag, "Erasing NVS due to %s", esp_err_to_name(err));
        ESP_ERROR_CHECK_WITHOUT_ABORT(nvs_flash_erase());
        err = nvs_flash_init();
    }
    return err == ESP_OK;
}

ConfigStatus readSlot(nvs_handle_t handle, const char* key, UserConfig& out, bool& migrated)
{
    migrated = false;
    size_t size = 0;
    esp_err_t err = nvs_get_blob(handle, key, nullptr, &size);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ConfigStatus::NotFound;
    }
    if (err != ESP_OK) {
        return ConfigStatus::Corrupt;
    }

    if (size == sizeof(UserConfigV1)) {
        UserConfigV1 legacy{};
        err = nvs_get_blob(handle, key, &legacy, &size);
        if (err != ESP_OK || legacy.schemaVersion != 1) {
            return ConfigStatus::Corrupt;
        }
        out = migrateUserConfigV1(legacy);
        migrated = true;
        return ConfigStatus::Ok;
    }

    if (size == sizeof(UserConfigV2)) {
        UserConfigV2 legacy{};
        err = nvs_get_blob(handle, key, &legacy, &size);
        if (err != ESP_OK || legacy.schemaVersion != 2) return ConfigStatus::Corrupt;
        out = migrateUserConfigV2(legacy);
        migrated = true;
        return ConfigStatus::Ok;
    }

    if (size == sizeof(UserConfig)) {
        UserConfig current{};
        err = nvs_get_blob(handle, key, &current, &size);
        if (err != ESP_OK) {
            return ConfigStatus::Corrupt;
        }
        out = current;
        return ConfigStatus::Ok;
    }

    return ConfigStatus::Corrupt;
}

}  // namespace

bool UserConfigStore::init()
{
    if (s_initialized) {
        return true;
    }
    if (!ensureNvs()) {
        ESP_LOGE(kTag, "NVS init failed");
        return false;
    }
    s_initialized = true;
    return true;
}

ConfigStatus UserConfigStore::validate(const UserConfig& config, const ProductDefinition& product)
{
    if (config.schemaVersion == 0 || config.schemaVersion > kUserConfigSchemaVersion) {
        return ConfigStatus::Invalid;
    }
    if (!config.provisioned) {
        return ConfigStatus::Invalid;
    }
    if (config.module != product.module) {
        return ConfigStatus::Invalid;
    }
    if (config.friendlyName[0] == '\0') {
        return ConfigStatus::Invalid;
    }
    if (!isValidDeviceHostname(config.hostname)) return ConfigStatus::Invalid;
    if (!config.connectors.localWebEnabled) {
        return ConfigStatus::Invalid;
    }
    if (config.connectors.mqttEnabled &&
        (config.connectors.mqttHost[0] == '\0' || config.connectors.mqttPort == 0 ||
         config.connectors.mqttDiscoveryPrefix[0] == '\0')) {
        return ConfigStatus::Invalid;
    }
    if (config.connectors.mqttEnabled && !product.supportsMqtt) {
        return ConfigStatus::Invalid;
    }
    if (config.connectors.matterEnabled && !product.supportsMatter) {
        return ConfigStatus::Invalid;
    }
    if (config.module == ModuleKind::Ac) {
        if (config.ac.profileId[0] == '\0') {
            return ConfigStatus::Invalid;
        }
        if (config.ac.bus == BusKind::Unknown || !productAllowsBus(product, config.ac.bus)) {
            return ConfigStatus::Invalid;
        }
    }
    return ConfigStatus::Ok;
}

ConfigLoadResult UserConfigStore::load()
{
    ConfigLoadResult result;
    if (!init()) {
        result.status = ConfigStatus::IoError;
        return result;
    }

    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(kNamespace, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        result.status = ConfigStatus::NotFound;
        return result;
    }
    if (err != ESP_OK) {
        result.status = ConfigStatus::IoError;
        return result;
    }

    uint8_t active = 0;
    err = nvs_get_u8(handle, kActiveSlotKey, &active);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        nvs_close(handle);
        result.status = ConfigStatus::NotFound;
        return result;
    }
    if (err != ESP_OK) {
        nvs_close(handle);
        result.status = ConfigStatus::Corrupt;
        return result;
    }

    if (active > 1) {
        nvs_close(handle);
        result.status = ConfigStatus::Corrupt;
        return result;
    }

    const char* key = (active == 0) ? kSlot0Key : kSlot1Key;
    bool migrated = false;
    ConfigStatus status = readSlot(handle, key, result.config, migrated);
    bool recoveredInactive = false;
    if (status != ConfigStatus::Ok) {
        const char* fallbackKey = (active == 0) ? kSlot1Key : kSlot0Key;
        status = readSlot(handle, fallbackKey, result.config, migrated);
        recoveredInactive = (status == ConfigStatus::Ok);
    }
    nvs_close(handle);

    if (status != ConfigStatus::Ok) {
        result.status = status;
        return result;
    }

    if (result.config.schemaVersion != kUserConfigSchemaVersion) {
        result.status = ConfigStatus::Invalid;
        return result;
    }

    if (migrated || recoveredInactive) {
        result.status = ConfigStatus::MigrationRequired;
        return result;
    }

    result.status = ConfigStatus::Ok;
    return result;
}

ConfigStatus UserConfigStore::save(const UserConfig& config)
{
    if (!init()) {
        return ConfigStatus::IoError;
    }

    const ProductDefinition& product = currentProduct();
    ConfigStatus valid = validate(config, product);
    if (valid != ConfigStatus::Ok) {
        return valid;
    }

    nvs_handle_t handle = 0;
    if (nvs_open(kNamespace, NVS_READWRITE, &handle) != ESP_OK) {
        return ConfigStatus::IoError;
    }

    uint8_t active = 0;
    esp_err_t err = nvs_get_u8(handle, kActiveSlotKey, &active);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        nvs_close(handle);
        return ConfigStatus::IoError;
    }

    const uint8_t next = (active == 0) ? 1 : 0;
    const char* key = (next == 0) ? kSlot0Key : kSlot1Key;

    if (nvs_set_blob(handle, key, &config, sizeof(UserConfig)) != ESP_OK) {
        nvs_close(handle);
        return ConfigStatus::IoError;
    }
    if (nvs_set_u8(handle, kActiveSlotKey, next) != ESP_OK) {
        nvs_close(handle);
        return ConfigStatus::IoError;
    }
    if (nvs_commit(handle) != ESP_OK) {
        nvs_close(handle);
        return ConfigStatus::IoError;
    }

    nvs_close(handle);
    ESP_LOGI(kTag, "Saved user config to slot %u", static_cast<unsigned>(next));
    return ConfigStatus::Ok;
}

namespace {
constexpr const char* kAcDesiredKey = "ac_desired";
constexpr uint8_t kAcDesiredVersion = 2;  // 2: vane position

struct StoredAcDesired {
    uint8_t version = kAcDesiredVersion;
    AcDesiredState state{};
};
}  // namespace

bool UserConfigStore::loadAcDesired(AcDesiredState& out)
{
    nvs_handle_t handle = 0;
    if (nvs_open(kNamespace, NVS_READONLY, &handle) != ESP_OK) {
        return false;
    }
    StoredAcDesired stored;
    size_t size = sizeof(stored);
    const esp_err_t err = nvs_get_blob(handle, kAcDesiredKey, &stored, &size);
    nvs_close(handle);
    if (err != ESP_OK || size != sizeof(stored) || stored.version != kAcDesiredVersion) {
        return false;
    }
    out = stored.state;
    return true;
}

ConfigStatus UserConfigStore::saveAcDesired(const AcDesiredState& state)
{
    nvs_handle_t handle = 0;
    if (nvs_open(kNamespace, NVS_READWRITE, &handle) != ESP_OK) {
        return ConfigStatus::IoError;
    }
    StoredAcDesired stored;
    stored.state = state;
    esp_err_t err = nvs_set_blob(handle, kAcDesiredKey, &stored, sizeof(stored));
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err == ESP_OK ? ConfigStatus::Ok : ConfigStatus::IoError;
}

ConfigStatus UserConfigStore::clear()
{
    if (!init()) {
        return ConfigStatus::IoError;
    }

    nvs_handle_t handle = 0;
    if (nvs_open(kNamespace, NVS_READWRITE, &handle) != ESP_OK) {
        return ConfigStatus::IoError;
    }

    nvs_erase_all(handle);
    const esp_err_t err = nvs_commit(handle);
    nvs_close(handle);
    return (err == ESP_OK) ? ConfigStatus::Ok : ConfigStatus::IoError;
}

}  // namespace opencomfort
