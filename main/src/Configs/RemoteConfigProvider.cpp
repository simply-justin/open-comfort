#include "src/Configs/RemoteConfigProvider.hpp"

extern "C" {
    #include "cJSON.h"
    #include "esp_crt_bundle.h"
    #include "esp_http_client.h"
    #include "esp_log.h"
    #include "mbedtls/sha256.h"
}

#include <sys/stat.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <utility>

#include "src/Configs/ConfigManager.hpp"

static const char* TAG = "RemoteConfigProvider";
static constexpr int kMaxManifestBytes = 16 * 1024;
static constexpr int kMaxProfileBytes = 64 * 1024;

static std::string sha256Hex(const std::string& content) {
    unsigned char digest[32];
    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);
    mbedtls_sha256_starts(&ctx, 0 /* SHA-256, not SHA-224 */);
    mbedtls_sha256_update(
        &ctx,
        reinterpret_cast<const unsigned char*>(content.data()),
        content.size()
    );
    mbedtls_sha256_finish(&ctx, digest);
    mbedtls_sha256_free(&ctx);

    char hex[65];
    for (size_t i = 0; i < sizeof(digest); ++i) {
        std::snprintf(hex + i * 2, 3, "%02x", digest[i]);
    }
    return std::string(hex, 64);
}

// Manifests may prefix the hash with its algorithm name (e.g.
// "sha256:<hex>"), matching the convention used by package-manifest-style
// checksums elsewhere (npm, Nix, etc.). A bare hex string is treated as
// already being a SHA-256 digest — the only algorithm this fetch path
// verifies against.
static bool checksumMatches(const std::string& expected, const std::string& content) {
    std::string hex = expected;
    const auto colon = hex.find(':');
    if (colon != std::string::npos) hex = hex.substr(colon + 1);

    std::string lowered = hex;
    std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char c) {
        return std::tolower(c);
    });

    return lowered == sha256Hex(content);
}

struct HttpBuffer {
    std::string* body;
    int maxBytes;
};

static esp_err_t httpEventHandler(esp_http_client_event_t* evt) {
    if (evt->event_id != HTTP_EVENT_ON_DATA || !evt->user_data || !evt->data) {
        return ESP_OK;
    }

    auto* buffer = static_cast<HttpBuffer*>(evt->user_data);
    if (static_cast<int>(buffer->body->size() + evt->data_len) > buffer->maxBytes) {
        ESP_LOGE(TAG, "HTTP response exceeded max size");
        return ESP_FAIL;
    }

    buffer->body->append(static_cast<const char*>(evt->data), evt->data_len);
    return ESP_OK;
}

RemoteConfigProvider::RemoteConfigProvider(std::string manifestUrl)
    : manifestUrl_(std::move(manifestUrl))
{}

bool RemoteConfigProvider::fetchRemoteUrl(
    const std::string& url,
    std::string& body,
    int maxBytes,
    std::string& error
) const {
    body.clear();
    HttpBuffer buffer{&body, maxBytes};

    esp_http_client_config_t config = {};
    config.url = url.c_str();
    config.event_handler = httpEventHandler;
    config.user_data = &buffer;
    config.crt_bundle_attach = esp_crt_bundle_attach;
    config.timeout_ms = 10000;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        error = "failed to initialize HTTP client";
        return false;
    }

    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK) {
        error = std::string("HTTP fetch failed: ") + esp_err_to_name(err);
        return false;
    }
    if (status < 200 || status >= 300) {
        error = "HTTP fetch returned status " + std::to_string(status);
        return false;
    }
    if (body.empty()) {
        error = "HTTP fetch returned an empty body";
        return false;
    }

    return true;
}

bool RemoteConfigProvider::fetchContent(
    const std::string& url,
    std::string& body,
    int maxBytes,
    std::string& error
) const {
    if (url.rfind("/spiffs/", 0) == 0) {
        body = ConfigManager::readTextFile(url.c_str());
        if (body.empty()) {
            error = "local file missing: " + url;
            return false;
        }
        if (static_cast<int>(body.size()) > maxBytes) {
            error = "local file is too large";
            body.clear();
            return false;
        }
        return true;
    }

    if (url.rfind("https://", 0) == 0 || url.rfind("http://", 0) == 0) {
        return fetchRemoteUrl(url, body, maxBytes, error);
    }

    error = "unsupported content URL: " + url;
    return false;
}

bool RemoteConfigProvider::loadManifest(std::string& manifest, std::string* error) const {
    std::string fetchError;
    if (!manifestUrl_.empty() &&
        fetchRemoteUrl(manifestUrl_, manifest, kMaxManifestBytes, fetchError)) {
        return true;
    }

    ESP_LOGW(TAG, "Remote manifest fetch failed (%s), trying bundled manifest",
             fetchError.c_str());

    manifest = ConfigManager::readTextFile(kBundledManifestPath);
    if (manifest.empty()) {
        if (error) *error = "remote manifest fetch failed and bundled manifest is missing";
        return false;
    }

    return true;
}

bool RemoteConfigProvider::parseProducts(
    const std::string& manifest,
    std::vector<ProductInfo>& products,
    std::string& error
) const {
    products.clear();

    cJSON* root = cJSON_Parse(manifest.c_str());
    if (!root) {
        error = "manifest is not valid JSON";
        return false;
    }

    cJSON* items = cJSON_GetObjectItem(root, "products");
    if (!cJSON_IsArray(items)) {
        error = "manifest.products must be an array";
        cJSON_Delete(root);
        return false;
    }

    cJSON* product = nullptr;
    cJSON_ArrayForEach(product, items) {
        if (!cJSON_IsObject(product)) continue;

        cJSON* id = cJSON_GetObjectItem(product, "id");
        cJSON* name = cJSON_GetObjectItem(product, "name");
        if (!cJSON_IsString(id)) continue;

        ProductInfo info;
        info.id = id->valuestring;
        info.name = cJSON_IsString(name) ? name->valuestring : info.id;

        cJSON* moduleType = cJSON_GetObjectItem(product, "module");
        if (cJSON_IsString(moduleType)) info.module = moduleType->valuestring;

        cJSON* bus = cJSON_GetObjectItem(product, "bus");
        if (cJSON_IsString(bus)) info.bus = bus->valuestring;

        cJSON* profileUrl = cJSON_GetObjectItem(product, "profileUrl");
        if (cJSON_IsString(profileUrl)) info.profileUrl = profileUrl->valuestring;

        cJSON* version = cJSON_GetObjectItem(product, "version");
        if (cJSON_IsString(version)) info.version = version->valuestring;

        products.push_back(std::move(info));
    }

    cJSON_Delete(root);

    if (products.empty()) {
        error = "manifest contains no products";
        return false;
    }

    return true;
}

bool RemoteConfigProvider::listProducts(std::vector<ProductInfo>& products, std::string* error) const {
    std::string manifest;
    if (!loadManifest(manifest, error)) return false;

    std::string parseError;
    if (!parseProducts(manifest, products, parseError)) {
        if (error) *error = parseError;
        return false;
    }

    return true;
}

RemoteProfileResult RemoteConfigProvider::fetchAndCacheProfile(const std::string& productId) {
    RemoteProfileResult result;
    result.productId = productId;

    std::string manifest;
    if (!loadManifest(manifest, &result.error)) {
        return result;
    }

    if (!resolveProfileFromManifest(manifest, productId, result)) {
        return result;
    }

    std::string profileJson;
    bool usedFallback = false;
    if (!fetchContent(result.profileUrl, profileJson, kMaxProfileBytes, result.error)) {
        const std::string fallbackPath = "/spiffs/profiles/" + productId + ".json";
        ESP_LOGW(TAG, "Profile fetch failed, trying bundled profile %s", fallbackPath.c_str());
        profileJson = ConfigManager::readTextFile(fallbackPath.c_str());
        if (profileJson.empty()) {
            if (result.error.empty()) result.error = "profile fetch failed";
            return result;
        }
        result.profileUrl = fallbackPath;
        usedFallback = true;
    }

    if (static_cast<int>(profileJson.size()) > kMaxProfileBytes) {
        result.error = "profile response is too large";
        return result;
    }

    // Only verify against the manifest's checksum when the content actually
    // came from the manifest-specified URL — a bundled offline-dev fallback
    // is a deliberately different (often older) copy, not the asset the
    // manifest's checksum describes, so checking it against that checksum
    // would fail every legitimate offline-dev run.
    if (!usedFallback && !result.checksum.empty() && !checksumMatches(result.checksum, profileJson)) {
        result.error = "profile checksum mismatch";
        return result;
    }

    if (!validateProfileJson(profileJson, result.error)) {
        return result;
    }

    mkdir("/spiffs/profiles", 0775);
    result.cachedPath = "/spiffs/profiles/" + productId + ".json";
    if (!ConfigManager::writeTextFile(result.cachedPath.c_str(), profileJson)) {
        result.error = "failed to cache profile";
        return result;
    }

    result.success = true;
    return result;
}

bool RemoteConfigProvider::resolveProfileFromManifest(
    const std::string& manifest,
    const std::string& productId,
    RemoteProfileResult& result
) const {
    cJSON* root = cJSON_Parse(manifest.c_str());
    if (!root) {
        result.error = "manifest is not valid JSON";
        return false;
    }

    cJSON* products = cJSON_GetObjectItem(root, "products");
    if (!cJSON_IsArray(products)) {
        result.error = "manifest.products must be an array";
        cJSON_Delete(root);
        return false;
    }

    cJSON* product = nullptr;
    cJSON_ArrayForEach(product, products) {
        cJSON* id = cJSON_GetObjectItem(product, "id");
        if (cJSON_IsString(id) && productId == id->valuestring) {
            cJSON* url = cJSON_GetObjectItem(product, "profileUrl");
            if (!cJSON_IsString(url)) {
                result.error = "selected product is missing profileUrl";
                cJSON_Delete(root);
                return false;
            }
            result.profileUrl = url->valuestring;

            cJSON* version = cJSON_GetObjectItem(product, "version");
            if (cJSON_IsString(version)) result.profileVersion = version->valuestring;

            cJSON* checksum = cJSON_GetObjectItem(product, "checksum");
            if (cJSON_IsString(checksum)) result.checksum = checksum->valuestring;

            cJSON_Delete(root);
            return true;
        }
    }

    result.error = "selected product was not found in manifest";
    cJSON_Delete(root);
    return false;
}

bool RemoteConfigProvider::validateProfileJson(const std::string& profileJson, std::string& error) const {
    cJSON* root = cJSON_Parse(profileJson.c_str());
    if (!root) {
        error = "profile is not valid JSON";
        return false;
    }

    cJSON* brand = cJSON_GetObjectItem(root, "brand");
    cJSON* protocol = cJSON_GetObjectItem(root, "protocol");
    cJSON* commands = cJSON_GetObjectItem(root, "commands");

    const bool valid = cJSON_IsString(brand) && cJSON_IsObject(protocol) && cJSON_IsObject(commands);
    if (!valid) {
        error = "profile must include brand, protocol, and commands";
    }

    cJSON_Delete(root);
    return valid;
}
