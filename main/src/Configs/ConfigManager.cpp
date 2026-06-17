#include "src/Configs/ConfigManager.hpp"

extern "C" {
    #include "esp_spiffs.h"
    #include "esp_log.h"
    #include "esp_err.h"
}
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "cJSON.h"

bool ConfigManager::s_initialized = false;
static const char* TAG = "ConfigManager";

bool ConfigManager::init() {
    if (s_initialized) return true;

    esp_vfs_spiffs_conf_t conf = {};
    conf.base_path = "/spiffs";
    conf.partition_label = nullptr;
    conf.max_files = 5;
    conf.format_if_mount_failed = true;

    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mount SPIFFS (%s)", esp_err_to_name(ret));
        return false;
    }

    size_t total = 0, used = 0;
    ret = esp_spiffs_info(nullptr, &total, &used);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get SPIFFS info (%s)", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "SPIFFS: total=%u, used=%u", (unsigned)total, (unsigned)used);
    }

    s_initialized = true;
    return true;
}

static std::string readFileToString(const char* path) {
    FILE* f = fopen(path, "r");
    if (!f) return {};

    std::string result;
    char buf[256];
    while (true) {
        size_t n = fread(buf, 1, sizeof(buf), f);
        if (n == 0) break;
        result.append(buf, n);
    }
    fclose(f);
    return result;
}

static bool writeStringToFile(const char* path, const std::string& data) {
    FILE* f = fopen(path, "w");
    if (!f) return false;
    size_t written = fwrite(data.data(), 1, data.size(), f);
    fclose(f);
    return written == data.size();
}

DeviceConfig ConfigManager::load() {
    DeviceConfig cfg;

    if (!init()) {
        ESP_LOGE(TAG, "SPIFFS not initialized, returning invalid config.");
        cfg.isValid = false;
        return cfg;
    }

    std::string json = readFileToString(kConfigPath);
    if (json.empty()) {
        ESP_LOGW(TAG, "No config file found, starting with invalid config.");
        cfg.isValid = false;
        return cfg;
    }

    cJSON* root = cJSON_Parse(json.c_str());
    if (!root) {
        ESP_LOGE(TAG, "Failed to parse config JSON.");
        cfg.isValid = false;
        return cfg;
    }

    // Basic fields
    cJSON* jVersion = cJSON_GetObjectItem(root, "configVersion");
    if (cJSON_IsNumber(jVersion)) {
        cfg.configVersion = jVersion->valueint;
    }

    cJSON* jValid = cJSON_GetObjectItem(root, "isValid");
    cfg.isValid = cJSON_IsBool(jValid) ? cJSON_IsTrue(jValid) : false;

    cJSON* jFriendly = cJSON_GetObjectItem(root, "friendlyName");
    if (cJSON_IsString(jFriendly)) {
        cfg.friendlyName = jFriendly->valuestring;
    }

    cJSON* jWifi = cJSON_GetObjectItem(root, "wifi");
    if (cJSON_IsObject(jWifi)) {
        cJSON* jSsid = cJSON_GetObjectItem(jWifi, "ssid");
        cJSON* jPass = cJSON_GetObjectItem(jWifi, "password");
        if (cJSON_IsString(jSsid)) cfg.wifiSsid = jSsid->valuestring;
        if (cJSON_IsString(jPass)) cfg.wifiPassword = jPass->valuestring;
    }

    // Module-specific
    cJSON* jModule = cJSON_GetObjectItem(root, "module");
    if (cJSON_IsObject(jModule)) {
        cJSON* jType = cJSON_GetObjectItem(jModule, "type");
        if (cJSON_IsString(jType)) {
            std::string typeStr = jType->valuestring;
            if (typeStr == "ac") {
                AcModuleConfig acCfg;
                acCfg.type = ModuleType::Ac;

                cJSON* jProfile = cJSON_GetObjectItem(jModule, "acProfilePath");
                cJSON* jName    = cJSON_GetObjectItem(jModule, "acFriendlyName");
                if (cJSON_IsString(jProfile)) acCfg.acProfilePath = jProfile->valuestring;
                if (cJSON_IsString(jName))    acCfg.acFriendlyName = jName->valuestring;

                cfg.moduleConfig = acCfg;
            }
            // Add other module types here later
        }
    }

    cJSON_Delete(root);
    return cfg;
}

bool ConfigManager::save(const DeviceConfig& cfg) {
    if (!init()) return false;

    cJSON* root = cJSON_CreateObject();

    cJSON_AddNumberToObject(root, "configVersion", cfg.configVersion);
    cJSON_AddBoolToObject(root, "isValid", cfg.isValid);
    cJSON_AddStringToObject(root, "friendlyName", cfg.friendlyName.c_str());

    cJSON* jWifi = cJSON_CreateObject();
    cJSON_AddStringToObject(jWifi, "ssid", cfg.wifiSsid.c_str());
    cJSON_AddStringToObject(jWifi, "password", cfg.wifiPassword.c_str());
    cJSON_AddItemToObject(root, "wifi", jWifi);

    // Module
    cJSON* jModule = cJSON_CreateObject();
    std::visit([&](auto&& mcfg) {
        using T = std::decay_t<decltype(mcfg)>;

        if constexpr (std::is_same_v<T, AcModuleConfig>) {
            cJSON_AddStringToObject(jModule, "type", "ac");
            cJSON_AddStringToObject(jModule, "acProfilePath", mcfg.acProfilePath.c_str());
            cJSON_AddStringToObject(jModule, "acFriendlyName", mcfg.acFriendlyName.c_str());
        }

        // Add other modules here with else if constexpr ...

    }, cfg.moduleConfig);
    cJSON_AddItemToObject(root, "module", jModule);

    char* jsonStr = cJSON_PrintUnformatted(root);
    std::string json(jsonStr ? jsonStr : "");
    cJSON_free(jsonStr);
    cJSON_Delete(root);

    if (json.empty()) {
        ESP_LOGE(TAG, "Failed to serialize config JSON.");
        return false;
    }

    if (!writeStringToFile(kConfigPath, json)) {
        ESP_LOGE(TAG, "Failed to write config JSON to %s", kConfigPath);
        return false;
    }

    ESP_LOGI(TAG, "Configuration saved to %s", kConfigPath);
    return true;
}
