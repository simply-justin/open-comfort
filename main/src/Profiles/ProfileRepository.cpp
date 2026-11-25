#include "src/Profiles/ProfileRepository.hpp"

#include <cstdio>
#include <cstring>
#include <memory>
#include <new>
#include <string>

#include "src/Profiles/HttpDownloader.hpp"
#include "src/Profiles/ProfileCrypto.hpp"
#include "src/Wifi/WiFiService.hpp"

extern "C" {
#include "cJSON.h"
#include "esp_log.h"
#include "esp_spiffs.h"
}

namespace opencomfort {
namespace {

constexpr const char* kTag = "ProfileRepository";
constexpr const char* kCatalogPath = "/spiffs/profiles/index.json";
constexpr const char* kRemoteCatalogPath = "/spiffs/profiles/remote.json";
constexpr size_t kMaxDownloadBytes = 48 * 1024;
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

bool writeFileAtomic(const char* path, const std::string& data)
{
    char tmpPath[160] = {};
    std::snprintf(tmpPath, sizeof(tmpPath), "%s.tmp", path);
    FILE* file = std::fopen(tmpPath, "w");
    if (file == nullptr) {
        return false;
    }
    const size_t written = std::fwrite(data.data(), 1, data.size(), file);
    std::fclose(file);
    if (written != data.size()) {
        std::remove(tmpPath);
        return false;
    }
    std::remove(path);
    return std::rename(tmpPath, path) == 0;
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

void appendCatalogEntries(ProfileCatalog& out, const cJSON* profiles)
{
    if (!cJSON_IsArray(profiles)) {
        return;
    }

    const cJSON* item = nullptr;
    cJSON_ArrayForEach(item, profiles)
    {
        if (out.count >= kMaxCatalogEntries) {
            break;
        }
        ProfileCatalogEntry& entry = out.entries[out.count];
        entry = ProfileCatalogEntry{};
        copyJsonString(entry.id, sizeof(entry.id), cJSON_GetObjectItem(item, "id"));
        copyJsonString(entry.makeId, sizeof(entry.makeId), cJSON_GetObjectItem(item, "make"));
        copyJsonString(entry.makeName, sizeof(entry.makeName), cJSON_GetObjectItem(item, "makeName"));
        copyJsonString(entry.profileVersion, sizeof(entry.profileVersion),
                       cJSON_GetObjectItem(item, "profileVersion"));
        copyJsonString(entry.relativePath, sizeof(entry.relativePath),
                       cJSON_GetObjectItem(item, "path"));
        copyJsonString(entry.url, sizeof(entry.url), cJSON_GetObjectItem(item, "url"));
        copyJsonString(entry.sha256, sizeof(entry.sha256), cJSON_GetObjectItem(item, "sha256"));
        copyJsonString(entry.signature, sizeof(entry.signature),
                       cJSON_GetObjectItem(item, "signature"));

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
        entry.bundled = cJSON_IsBool(bundled) ? cJSON_IsTrue(bundled) : (entry.url[0] == '\0');

        if (entry.id[0] != '\0' && entry.bus != BusKind::Unknown) {
            out.count++;
        }
    }
}

const ProfileCatalogEntry* findEntry(const ProfileCatalog& catalog, const char* profileId)
{
    for (size_t i = 0; i < catalog.count; ++i) {
        if (std::strcmp(catalog.entries[i].id, profileId) == 0) {
            return &catalog.entries[i];
        }
    }
    return nullptr;
}

esp_err_t parseProfileMetadata(const std::string& json, const ProfileCatalogEntry* match,
                               LoadedAcProfile& out)
{
    out = LoadedAcProfile{};
    cJSON* root = cJSON_Parse(json.c_str());
    if (root == nullptr) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    copyJsonString(out.profileId, sizeof(out.profileId), cJSON_GetObjectItem(root, "profileId"));
    if (out.profileId[0] == '\0' && match != nullptr) {
        std::strncpy(out.profileId, match->id, sizeof(out.profileId) - 1);
    }
    copyJsonString(out.brand, sizeof(out.brand), cJSON_GetObjectItem(root, "brand"));
    copyJsonString(out.profileVersion, sizeof(out.profileVersion),
                   cJSON_GetObjectItem(root, "profileVersion"));
    if (out.profileVersion[0] == '\0' && match != nullptr) {
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

    if (out.bus == BusKind::Unknown && match != nullptr) {
        out.bus = match->bus;
    }
    if (out.baudOrClockHz <= 0) {
        out.baudOrClockHz = (out.bus == BusKind::Spi) ? 1000000 : 9600;
    }

    out.valid = (out.profileId[0] != '\0' && out.bus != BusKind::Unknown);
    cJSON_Delete(root);
    return out.valid ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
}

esp_err_t verifyDownloadedProfile(const ProfileCatalogEntry& entry, const std::string& body)
{
    if (entry.sha256[0] == '\0' || entry.signature[0] == '\0') {
        ESP_LOGE(kTag, "Remote profile missing sha256/signature");
        return ESP_ERR_INVALID_RESPONSE;
    }

    char digest[65] = {};
    esp_err_t err = ProfileCrypto::sha256Hex(reinterpret_cast<const uint8_t*>(body.data()),
                                             body.size(), digest);
    if (err != ESP_OK) {
        return err;
    }
    if (std::strcmp(digest, entry.sha256) != 0) {
        ESP_LOGE(kTag, "Profile sha256 mismatch");
        return ESP_ERR_INVALID_CRC;
    }

    char message[160] = {};
    std::snprintf(message, sizeof(message), "%s|%s|%s", entry.id, entry.sha256, entry.profileVersion);
    return ProfileCrypto::verifyEcdsaSignatureHex(reinterpret_cast<const uint8_t*>(message),
                                                  std::strlen(message), entry.signature);
}

esp_err_t downloadProfile(const ProfileCatalogEntry& entry, LoadedAcProfile& out)
{
    if (entry.url[0] == '\0') {
        return ESP_ERR_NOT_FOUND;
    }
    if (!WiFiService::isStationConnected()) {
        return ESP_ERR_INVALID_STATE;
    }

    std::string body;
    esp_err_t err = HttpDownloader::getHttps(entry.url, body, kMaxDownloadBytes);
    if (err != ESP_OK) {
        return err;
    }

    err = verifyDownloadedProfile(entry, body);
    if (err != ESP_OK) {
        return err;
    }

    char localPath[128] = {};
    // Keep SPIFFS object names short.
    std::snprintf(localPath, sizeof(localPath), "/spiffs/profiles/d_%.8s.json", entry.id);

    if (!writeFileAtomic(localPath, body)) {
        return ESP_FAIL;
    }

    err = parseProfileMetadata(body, &entry, out);
    if (err != ESP_OK) {
        return err;
    }
    std::strncpy(out.localPath, localPath, sizeof(out.localPath) - 1);
    return ESP_OK;
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
    // Reset in place: assigning ProfileCatalog{} builds a ~50 KB temporary on the stack.
    out.~ProfileCatalog();
    new (&out) ProfileCatalog();
    esp_err_t err = initSpiffs();
    if (err != ESP_OK) {
        return err;
    }

    const std::string bundled = readFile(kCatalogPath);
    if (bundled.empty()) {
        return ESP_ERR_NOT_FOUND;
    }
    cJSON* root = cJSON_Parse(bundled.c_str());
    if (root == nullptr) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    const cJSON* schema = cJSON_GetObjectItem(root, "schemaVersion");
    if (cJSON_IsNumber(schema)) {
        out.schemaVersion = static_cast<uint16_t>(schema->valueint);
    }
    appendCatalogEntries(out, cJSON_GetObjectItem(root, "profiles"));
    cJSON_Delete(root);

    const std::string remote = readFile(kRemoteCatalogPath);
    if (!remote.empty()) {
        cJSON* remoteRoot = cJSON_Parse(remote.c_str());
        if (remoteRoot != nullptr) {
            appendCatalogEntries(out, cJSON_GetObjectItem(remoteRoot, "profiles"));
            cJSON_Delete(remoteRoot);
        }
    }
    return ESP_OK;
}

esp_err_t ProfileRepository::refreshRemoteCatalog(const ProductDefinition& product)
{
    if (product.profileCatalogUrl == nullptr || product.profileCatalogUrl[0] == '\0') {
        return ESP_OK;
    }
    if (!WiFiService::isStationConnected()) {
        return ESP_ERR_INVALID_STATE;
    }

    std::string body;
    esp_err_t err = HttpDownloader::getHttps(product.profileCatalogUrl, body, kMaxDownloadBytes);
    if (err != ESP_OK) {
        return err;
    }

    cJSON* root = cJSON_Parse(body.c_str());
    if (root == nullptr || !cJSON_IsArray(cJSON_GetObjectItem(root, "profiles"))) {
        cJSON_Delete(root);
        return ESP_ERR_INVALID_RESPONSE;
    }
    cJSON_Delete(root);

    if (!writeFileAtomic(kRemoteCatalogPath, body)) {
        return ESP_FAIL;
    }
    ESP_LOGI(kTag, "Remote catalog refreshed");
    return ESP_OK;
}

esp_err_t ProfileRepository::loadProfile(const char* profileId, LoadedAcProfile& out)
{
    out = LoadedAcProfile{};
    if (profileId == nullptr || profileId[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    std::unique_ptr<ProfileCatalog> catalog(new (std::nothrow) ProfileCatalog());
    if (!catalog) {
        return ESP_ERR_NO_MEM;
    }
    esp_err_t err = loadCatalog(*catalog);
    if (err != ESP_OK) {
        return err;
    }

    const ProfileCatalogEntry* match = findEntry(*catalog, profileId);
    if (match == nullptr) {
        return ESP_ERR_NOT_FOUND;
    }

    char path[128] = {};
    if (match->relativePath[0] != '\0') {
        std::snprintf(path, sizeof(path), "/spiffs/%s", match->relativePath);
    } else {
        std::snprintf(path, sizeof(path), "/spiffs/profiles/d_%.8s.json", match->id);
    }

    const std::string json = readFile(path);
    if (json.empty()) {
        return ESP_ERR_NOT_FOUND;
    }

    err = parseProfileMetadata(json, match, out);
    if (err != ESP_OK) {
        return err;
    }
    std::strncpy(out.localPath, path, sizeof(out.localPath) - 1);
    return ESP_OK;
}

esp_err_t ProfileRepository::loadProfileJson(const char* profileId, std::string& outJson)
{
    LoadedAcProfile meta;
    esp_err_t err = loadProfile(profileId, meta);
    if (err != ESP_OK) {
        return err;
    }
    outJson = readFile(meta.localPath);
    return outJson.empty() ? ESP_ERR_NOT_FOUND : ESP_OK;
}

esp_err_t ProfileRepository::ensureProfileAvailable(const ProductDefinition& product,
                                                    const char* profileId,
                                                    LoadedAcProfile& out)
{
    esp_err_t err = loadProfile(profileId, out);
    if (err == ESP_OK) {
        if (!productAllowsBus(product, out.bus)) {
            out.valid = false;
            return ESP_ERR_NOT_SUPPORTED;
        }
        return ESP_OK;
    }

    // Attempt remote catalog refresh + download when configured.
    if (product.profileCatalogUrl != nullptr && product.profileCatalogUrl[0] != '\0') {
        (void)refreshRemoteCatalog(product);
        std::unique_ptr<ProfileCatalog> catalog(new (std::nothrow) ProfileCatalog());
        if (catalog && loadCatalog(*catalog) == ESP_OK) {
            const ProfileCatalogEntry* match = findEntry(*catalog, profileId);
            if (match != nullptr && match->url[0] != '\0') {
                err = downloadProfile(*match, out);
                if (err == ESP_OK) {
                    if (!productAllowsBus(product, out.bus)) {
                        out.valid = false;
                        return ESP_ERR_NOT_SUPPORTED;
                    }
                    return ESP_OK;
                }
            }
        }
    }

    return err;
}

}  // namespace opencomfort
