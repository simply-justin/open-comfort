#include "src/Config/ConnectorSecretStore.hpp"

#include <cstring>

extern "C" {
#include "esp_random.h"
#include "mbedtls/md.h"
#include "mbedtls/pkcs5.h"
#include "nvs.h"
}

namespace opencomfort {
namespace {

constexpr const char* kNamespace = "oc_secrets";
constexpr const char* kMqttKey = "mqtt";
constexpr const char* kAdminKey = "admin";
constexpr uint32_t kPbkdf2Iterations = 50000;
constexpr size_t kSaltSize = 16;
constexpr size_t kHashSize = 32;
constexpr size_t kMinAdminPasswordLength = 8;
constexpr size_t kMaxAdminPasswordLength = 64;

struct AdminCredential {
    uint8_t salt[kSaltSize] = {};
    uint8_t hash[kHashSize] = {};
    uint32_t iterations = kPbkdf2Iterations;
};

esp_err_t derive(const char* password, const AdminCredential& credential, uint8_t out[kHashSize])
{
    if (password == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    const int rc = mbedtls_pkcs5_pbkdf2_hmac_ext(
        MBEDTLS_MD_SHA256, reinterpret_cast<const unsigned char*>(password), std::strlen(password),
        credential.salt, sizeof(credential.salt), credential.iterations, kHashSize, out);
    return rc == 0 ? ESP_OK : ESP_FAIL;
}

bool constantTimeEqual(const uint8_t* left, const uint8_t* right, size_t length)
{
    uint8_t difference = 0;
    for (size_t i = 0; i < length; ++i) {
        difference |= left[i] ^ right[i];
    }
    return difference == 0;
}

esp_err_t readBlob(const char* key, void* value, size_t expectedSize)
{
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(kNamespace, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return err;
    }
    size_t size = expectedSize;
    err = nvs_get_blob(handle, key, value, &size);
    nvs_close(handle);
    return (err == ESP_OK && size == expectedSize) ? ESP_OK :
           (err == ESP_OK ? ESP_ERR_INVALID_SIZE : err);
}

esp_err_t writeBlob(const char* key, const void* value, size_t size)
{
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(kNamespace, NVS_READWRITE, &handle);
    if (err == ESP_OK) {
        err = nvs_set_blob(handle, key, value, size);
    }
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    if (handle != 0) {
        nvs_close(handle);
    }
    return err;
}

esp_err_t eraseKey(const char* key)
{
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(kNamespace, NVS_READWRITE, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_erase_key(handle, key);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        err = ESP_OK;
    }
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}

}  // namespace

esp_err_t ConnectorSecretStore::saveMqttCredentials(const MqttCredentials& credentials)
{
    return writeBlob(kMqttKey, &credentials, sizeof(credentials));
}

esp_err_t ConnectorSecretStore::loadMqttCredentials(MqttCredentials& credentials)
{
    credentials = {};
    return readBlob(kMqttKey, &credentials, sizeof(credentials));
}

esp_err_t ConnectorSecretStore::clearMqttCredentials()
{
    return eraseKey(kMqttKey);
}

esp_err_t ConnectorSecretStore::setAdminPassword(const char* password)
{
    if (password == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    const size_t length = std::strlen(password);
    if (length < kMinAdminPasswordLength || length > kMaxAdminPasswordLength) {
        return ESP_ERR_INVALID_ARG;
    }

    AdminCredential credential;
    esp_fill_random(credential.salt, sizeof(credential.salt));
    esp_err_t err = derive(password, credential, credential.hash);
    if (err != ESP_OK) {
        return err;
    }
    return writeBlob(kAdminKey, &credential, sizeof(credential));
}

bool ConnectorSecretStore::adminPasswordConfigured()
{
    AdminCredential credential;
    return readBlob(kAdminKey, &credential, sizeof(credential)) == ESP_OK;
}

bool ConnectorSecretStore::verifyAdminPassword(const char* password)
{
    AdminCredential credential;
    if (readBlob(kAdminKey, &credential, sizeof(credential)) != ESP_OK ||
        credential.iterations < 10000 || credential.iterations > 500000) {
        return false;
    }
    uint8_t candidate[kHashSize] = {};
    if (derive(password, credential, candidate) != ESP_OK) {
        return false;
    }
    return constantTimeEqual(candidate, credential.hash, sizeof(candidate));
}

esp_err_t ConnectorSecretStore::clear()
{
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(kNamespace, NVS_READWRITE, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_erase_all(handle);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}

}  // namespace opencomfort
