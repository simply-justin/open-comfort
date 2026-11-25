#include "src/Config/DeviceHostname.hpp"

#include <cstdio>
#include <cstring>

extern "C" {
#include "esp_mac.h"
}

namespace opencomfort {

bool isValidDeviceHostname(const char* hostname)
{
    if (hostname == nullptr) return false;
    const size_t length = strnlen(hostname, kMaxHostnameLen + 1);
    if (length > kMaxHostnameLen) return false;
    if (length == 0) return true;
    if (hostname[0] == '-' || hostname[length - 1] == '-') return false;
    for (size_t i = 0; i < length; ++i) {
        const char c = hostname[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-')) return false;
    }
    return true;
}

bool normalizeDeviceHostname(const char* input, char* out, size_t size)
{
    if (input == nullptr || out == nullptr || size == 0) return false;
    const size_t length = strnlen(input, kMaxHostnameLen + 7);
    if (length > kMaxHostnameLen + 6) return false;
    char normalized[kMaxHostnameLen + 7] = {};
    for (size_t i = 0; i < length; ++i) {
        const char c = input[i];
        normalized[i] = (c >= 'A' && c <= 'Z') ? c + ('a' - 'A') : c;
    }
    if (length >= 6 && std::strcmp(normalized + length - 6, ".local") == 0) {
        if (length == 6) return false;
        normalized[length - 6] = '\0';
    }
    if (!isValidDeviceHostname(normalized) || std::strlen(normalized) >= size) return false;
    copyBounded(out, size, normalized);
    return true;
}

esp_err_t resolveDeviceHostname(const UserConfig& config, char* out, size_t size)
{
    if (out == nullptr || !isValidDeviceHostname(config.hostname)) return ESP_ERR_INVALID_ARG;
    if (config.hostname[0] != '\0') {
        if (std::strlen(config.hostname) >= size) return ESP_ERR_INVALID_SIZE;
        copyBounded(out, size, config.hostname);
        return ESP_OK;
    }
    uint8_t mac[6] = {};
    const esp_err_t err = esp_read_mac(mac, ESP_MAC_WIFI_STA);
    if (err != ESP_OK) return err;
    const int length = std::snprintf(out, size, "opencomfort-%02x%02x", mac[4], mac[5]);
    return length >= 0 && static_cast<size_t>(length) < size ? ESP_OK : ESP_ERR_INVALID_SIZE;
}

}  // namespace opencomfort
