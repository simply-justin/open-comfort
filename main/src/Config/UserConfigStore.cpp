#include "src/Config/UserConfigStore.hpp"

#include <cstring>

extern "C" {
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"
}

namespace opencomfort {
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

ConfigStatus readSlot(nvs_handle_t handle, const char* key, UserConfig& out)
{
    size_t size = sizeof(UserConfig);
    UserConfig tmp = {};
    esp_err_t err = nvs_get_blob(handle, key, &tmp, &size);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ConfigStatus::NotFound;
    }
    if (err != ESP_OK || size != sizeof(UserConfig)) {
        return ConfigStatus::Corrupt;
    }
    out = tmp;
    return ConfigStatus::Ok;
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

    const char* key = (active == 0) ? kSlot0Key : kSlot1Key;
    ConfigStatus status = readSlot(handle, key, result.config);
    nvs_close(handle);

    if (status != ConfigStatus::Ok) {
        result.status = status;
        return result;
    }

    if (result.config.schemaVersion != kUserConfigSchemaVersion) {
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
