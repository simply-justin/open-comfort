#pragma once

#include <cstddef>

#include "esp_err.h"

namespace opencomfort {

constexpr size_t kMaxMqttUsernameLen = 64;
constexpr size_t kMaxMqttPasswordLen = 128;

struct MqttCredentials {
    char username[kMaxMqttUsernameLen + 1] = {};
    char password[kMaxMqttPasswordLen + 1] = {};
};

class ConnectorSecretStore {
public:
    static esp_err_t saveMqttCredentials(const MqttCredentials& credentials);
    static esp_err_t loadMqttCredentials(MqttCredentials& credentials);
    static esp_err_t clearMqttCredentials();

    static esp_err_t setAdminPassword(const char* password);
    static bool adminPasswordConfigured();
    static bool verifyAdminPassword(const char* password);
    static esp_err_t clear();
};

}  // namespace opencomfort
