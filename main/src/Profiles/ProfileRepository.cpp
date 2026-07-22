#include "src/Profiles/ProfileRepository.hpp"

#include <cstdio>
#include <cstring>
#include <string>

extern "C" {
#include "cJSON.h"
#include "esp_log.h"
#include "esp_spiffs.h"
}

namespace opencomfort {
namespace {

constexpr const char* kTag = "ProfileRepository";
constexpr const char* kCatalogPath = "/spiffs/profiles/index.json";
bool s_spiffsReady = false;

std::string readFile(const char* path)
{
    FILE* file = std::fopen(path, "r");
    if (file == nullptr) {
        return {};
    }

    std::string data;
    char buffer[256];
    while (true) {
        const size_t n = std::fread(buffer, 1, sizeof(buffer), file);
        if (n == 0) {
            break;
        }
        data.append(buffer, n);
        if (data.size() > 64 * 1024) {
            std::fclose(file);
            return {};
        }
    }
    std::fclose(file);
    return data;
}

void copyJsonString(char* dest, size_t destSize, const cJSON* value)
{
    if (dest == nullptr || destSize == 0) {
        return;
    }
    dest[0] = '\0';
    if (cJSON_IsString(value) && value->valuestring != nullptr) {
        std::strncpy(dest, value->valuestring, destSize - 1);
        dest[destSize - 1] = '\0';
    }
}

}  // namespace

esp_err_t ProfileRepository::initSpiffs()
{
    if (s_spiffsReady) {
        return ESP_OK;
    }

    esp_vfs_spiffs_conf_t conf = {};
    conf.base_path = "/spiffs";
    conf.partition_label = "storage";
    conf.max_files = 8;
    conf.format_if_mount_failed = false;

    const esp_err_t err = esp_vfs_spiffs_register(&conf);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "SPIFFS mount failed: %s", esp_err_to_name(err));
        return err;
    }

    size_t total = 0;
    size_t used = 0;
    if (esp_spiffs_info("storage", &total, &used) == ESP_OK) {
        ESP_LOGI(kTag, "SPIFFS mounted total=%u used=%u", static_cast<unsigned>(total),
                 static_cast<unsigned>(used));
    }

    s_spiffsReady = true;
    return ESP_OK;
}

esp_err_t ProfileRepository::loadCatalog(ProfileCatalog& out)
{
    out = ProfileCatalog{};
    esp_err_t err = initSpiffs();
    if (err != ESP_OK) {
        return err;
    }

    const std::string json = readFile(kCatalogPath);
    if (json.empty()) {
        return ESP_ERR_NOT_FOUND;
    }

    cJSON* root = cJSON_Parse(json.c_str());
    if (root == nullptr) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    const cJSON* schema = cJSON_GetObjectItem(root, "schemaVersion");
    if (cJSON_IsNumber(schema)) {
        out.schemaVersion = static_cast<uint16_t>(schema->valueint);
    }

    const cJSON* profiles = cJSON_GetObjectItem(root, "profiles");
    if (!cJSON_IsArray(profiles)) {
        cJSON_Delete(root);
        return ESP_ERR_INVALID_RESPONSE;
    }

    const cJSON* item = nullptr;
    cJSON_ArrayForEach(item, profiles)
    {
        if (out.count >= kMaxCatalogEntries) {
            break;
        }
        ProfileCatalogEntry& entry = out.entries[out.count];
        copyJsonString(entry.id, sizeof(entry.id), cJSON_GetObjectItem(item, "id"));
        copyJsonString(entry.makeId, sizeof(entry.makeId), cJSON_GetObjectItem(item, "make"));
        copyJsonString(entry.makeName, sizeof(entry.makeName), cJSON_GetObjectItem(item, "makeName"));
        copyJsonString(entry.profileVersion, sizeof(entry.profileVersion),
                       cJSON_GetObjectItem(item, "profileVersion"));
        copyJsonString(entry.relativePath, sizeof(entry.relativePath),
                       cJSON_GetObjectItem(item, "path"));

        const cJSON* bus = cJSON_GetObjectItem(item, "bus");
        if (cJSON_IsString(bus)) {
            entry.bus = busKindFromString(bus->valuestring);
        }

        const cJSON* models = cJSON_GetObjectItem(item, "models");
        if (cJSON_IsArray(models)) {
            const cJSON* model = nullptr;
            cJSON_ArrayForEach(model, models)
            {
                if (entry.modelCount >= kMaxModelsPerProfile) {
                    break;
                }
                copyJsonString(entry.models[entry.modelCount], sizeof(entry.models[0]), model);
                if (entry.models[entry.modelCount][0] != '\0') {
                    entry.modelCount++;
                }
            }
        }

        const cJSON* bundled = cJSON_GetObjectItem(item, "bundled");
        entry.bundled = cJSON_IsBool(bundled) ? cJSON_IsTrue(bundled) : true;

        if (entry.id[0] != '\0' && entry.bus != BusKind::Unknown) {
            out.count++;
        }
    }

    cJSON_Delete(root);
    return ESP_OK;
}

esp_err_t ProfileRepository::loadProfile(const char* profileId, LoadedAcProfile& out)
{
    out = LoadedAcProfile{};
    if (profileId == nullptr || profileId[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    ProfileCatalog catalog;
    esp_err_t err = loadCatalog(catalog);
    if (err != ESP_OK) {
        return err;
    }

    const ProfileCatalogEntry* match = nullptr;
    for (size_t i = 0; i < catalog.count; ++i) {
        if (std::strcmp(catalog.entries[i].id, profileId) == 0) {
            match = &catalog.entries[i];
            break;
        }
    }
    if (match == nullptr) {
        return ESP_ERR_NOT_FOUND;
    }

    char path[128] = {};
    std::snprintf(path, sizeof(path), "/spiffs/%s", match->relativePath);
    const std::string json = readFile(path);
    if (json.empty()) {
        return ESP_ERR_NOT_FOUND;
    }

    cJSON* root = cJSON_Parse(json.c_str());
    if (root == nullptr) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    copyJsonString(out.profileId, sizeof(out.profileId), cJSON_GetObjectItem(root, "profileId"));
    if (out.profileId[0] == '\0') {
        std::strncpy(out.profileId, match->id, sizeof(out.profileId) - 1);
    }
    copyJsonString(out.brand, sizeof(out.brand), cJSON_GetObjectItem(root, "brand"));
    copyJsonString(out.profileVersion, sizeof(out.profileVersion),
                   cJSON_GetObjectItem(root, "profileVersion"));
    if (out.profileVersion[0] == '\0') {
        std::strncpy(out.profileVersion, match->profileVersion, sizeof(out.profileVersion) - 1);
    }

    const cJSON* protocol = cJSON_GetObjectItem(root, "protocol");
    if (cJSON_IsObject(protocol)) {
        const cJSON* bus = cJSON_GetObjectItem(protocol, "bus");
        if (cJSON_IsString(bus)) {
            out.bus = busKindFromString(bus->valuestring);
        }
        const cJSON* frequency = cJSON_GetObjectItem(protocol, "frequencyHz");
        if (cJSON_IsNumber(frequency)) {
            out.baudOrClockHz = frequency->valueint;
        }
        const cJSON* baud = cJSON_GetObjectItem(protocol, "baudRate");
        if (cJSON_IsNumber(baud)) {
            out.baudOrClockHz = baud->valueint;
        }
        const cJSON* mode = cJSON_GetObjectItem(protocol, "mode");
        if (cJSON_IsNumber(mode)) {
            out.spiMode = static_cast<uint8_t>(mode->valueint);
        } else if (cJSON_IsString(mode) && mode->valuestring != nullptr) {
            if (std::strcmp(mode->valuestring, "mode0") == 0) {
                out.spiMode = 0;
            } else if (std::strcmp(mode->valuestring, "mode1") == 0) {
                out.spiMode = 1;
            } else if (std::strcmp(mode->valuestring, "mode2") == 0) {
                out.spiMode = 2;
            } else if (std::strcmp(mode->valuestring, "mode3") == 0) {
                out.spiMode = 3;
            }
        }
    }

    if (out.bus == BusKind::Unknown) {
        out.bus = match->bus;
    }
    if (out.baudOrClockHz <= 0) {
        out.baudOrClockHz = (out.bus == BusKind::Spi) ? 1000000 : 9600;
    }

    out.valid = (out.profileId[0] != '\0' && out.bus != BusKind::Unknown);
    cJSON_Delete(root);
    return out.valid ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
}

esp_err_t ProfileRepository::ensureProfileAvailable(const ProductDefinition& product,
                                                    const char* profileId,
                                                    LoadedAcProfile& out)
{
    esp_err_t err = loadProfile(profileId, out);
    if (err != ESP_OK) {
        return err;
    }
    if (!productAllowsBus(product, out.bus)) {
        out.valid = false;
        return ESP_ERR_NOT_SUPPORTED;
    }
    // Remote download can be added later when catalog URL is configured.
    (void)product.profileCatalogUrl;
    return ESP_OK;
}

}  // namespace opencomfort
