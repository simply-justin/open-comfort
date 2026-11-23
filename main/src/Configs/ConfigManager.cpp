#include "src/Configs/ConfigManager.hpp"

extern "C" {
    #include "esp_err.h"
    #include "esp_log.h"
    #include "esp_spiffs.h"
}
#include <cstdio>
#include <string>
#include <type_traits>

#include "cJSON.h"

bool ConfigManager::s_initialized = false;
static const char* TAG = "ConfigManager";

bool ConfigManager::init() {
    if (s_initialized) return true;

    esp_vfs_spiffs_conf_t conf = {};
    conf.base_path = "/spiffs";
    conf.partition_label = "storage";
    conf.max_files = 5;
    conf.format_if_mount_failed = true;

    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mount SPIFFS (%s)", esp_err_to_name(ret));
        return false;
    }

    size_t total = 0, used = 0;
    ret = esp_spiffs_info("storage", &total, &used);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get SPIFFS info (%s)", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "SPIFFS: total=%u, used=%u", (unsigned)total, (unsigned)used);
    }

    s_initialized = true;
    return true;
}

std::string ConfigManager::readTextFile(const char* path) {
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

bool ConfigManager::writeTextFile(const char* path, const std::string& data) {
    FILE* f = fopen(path, "w");
    if (!f) return false;
    size_t written = fwrite(data.data(), 1, data.size(), f);
    fclose(f);
    return written == data.size();
}

static std::string getString(cJSON* object, const char* key, const std::string& fallback = {}) {
    cJSON* item = cJSON_GetObjectItem(object, key);
    return cJSON_IsString(item) ? item->valuestring : fallback;
}

static int getInt(cJSON* object, const char* key, int fallback) {
    cJSON* item = cJSON_GetObjectItem(object, key);
    return cJSON_IsNumber(item) ? item->valueint : fallback;
}

static Bus parseBus(cJSON* module) {
    cJSON* bus = cJSON_GetObjectItem(module, "bus");
    if (!cJSON_IsObject(bus)) return SpiConfig{};

    const std::string type = getString(bus, "type", "spi");
    if (type == "uart" || type == "rs485") {
        UartConfig cfg;
        cfg.port = getInt(bus, "port", cfg.port);
        cfg.txPin = getInt(bus, "txPin", cfg.txPin);
        cfg.rxPin = getInt(bus, "rxPin", cfg.rxPin);
        cfg.baudRate = getInt(bus, "baudRate", cfg.baudRate);
        return cfg;
    }
    if (type == "can") {
        CanConfig cfg;
        cfg.txPin = getInt(bus, "txPin", cfg.txPin);
        cfg.rxPin = getInt(bus, "rxPin", cfg.rxPin);
        cfg.baudRate = getInt(bus, "baudRate", cfg.baudRate);
        cfg.id = static_cast<uint32_t>(getInt(bus, "id", static_cast<int>(cfg.id)));
        return cfg;
    }

    SpiConfig cfg;
    cfg.host = getInt(bus, "host", cfg.host);
    cfg.sckPin = getInt(bus, "sckPin", cfg.sckPin);
    cfg.mosiPin = getInt(bus, "mosiPin", cfg.mosiPin);
    cfg.misoPin = getInt(bus, "misoPin", cfg.misoPin);
    cfg.csPin = getInt(bus, "csPin", cfg.csPin);
    cfg.mode = getInt(bus, "mode", cfg.mode);
    cfg.frequency = getInt(bus, "frequency", cfg.frequency);
    return cfg;
}

static void addBusJson(cJSON* module, const Bus& bus) {
    cJSON* jBus = cJSON_CreateObject();
    std::visit([&](const auto& cfg) {
        using T = std::decay_t<decltype(cfg)>;
        if constexpr (std::is_same_v<T, UartConfig>) {
            cJSON_AddStringToObject(jBus, "type", "uart");
            cJSON_AddNumberToObject(jBus, "port", cfg.port);
            cJSON_AddNumberToObject(jBus, "txPin", cfg.txPin);
            cJSON_AddNumberToObject(jBus, "rxPin", cfg.rxPin);
            cJSON_AddNumberToObject(jBus, "baudRate", cfg.baudRate);
        } else if constexpr (std::is_same_v<T, CanConfig>) {
            cJSON_AddStringToObject(jBus, "type", "can");
            cJSON_AddNumberToObject(jBus, "txPin", cfg.txPin);
            cJSON_AddNumberToObject(jBus, "rxPin", cfg.rxPin);
            cJSON_AddNumberToObject(jBus, "baudRate", cfg.baudRate);
            cJSON_AddNumberToObject(jBus, "id", cfg.id);
        } else if constexpr (std::is_same_v<T, SpiConfig>) {
            cJSON_AddStringToObject(jBus, "type", "spi");
            cJSON_AddNumberToObject(jBus, "host", cfg.host);
            cJSON_AddNumberToObject(jBus, "sckPin", cfg.sckPin);
            cJSON_AddNumberToObject(jBus, "mosiPin", cfg.mosiPin);
            cJSON_AddNumberToObject(jBus, "misoPin", cfg.misoPin);
            cJSON_AddNumberToObject(jBus, "csPin", cfg.csPin);
            cJSON_AddNumberToObject(jBus, "mode", cfg.mode);
            cJSON_AddNumberToObject(jBus, "frequency", cfg.frequency);
        }
    }, bus);
    cJSON_AddItemToObject(module, "bus", jBus);
}

DeviceConfig ConfigManager::load() {
    DeviceConfig cfg;

    if (!init()) {
        ESP_LOGE(TAG, "SPIFFS not initialized, returning invalid config.");
        cfg.isValid = false;
        return cfg;
    }

    std::string json = readTextFile(kConfigPath);
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

    cJSON* jVersion = cJSON_GetObjectItem(root, "configVersion");
    if (cJSON_IsNumber(jVersion)) {
        cfg.configVersion = jVersion->valueint;
    }

    cJSON* jValid = cJSON_GetObjectItem(root, "isValid");
    cfg.isValid = cJSON_IsBool(jValid) ? cJSON_IsTrue(jValid) : false;

    cfg.friendlyName = getString(root, "friendlyName", cfg.friendlyName);
    cfg.selectedProductId = getString(root, "selectedProductId", cfg.selectedProductId);

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
                acCfg.productId = getString(jModule, "productId", cfg.selectedProductId);
                acCfg.acProfilePath = getString(jModule, "acProfilePath", acCfg.acProfilePath);
                acCfg.acFriendlyName = getString(jModule, "acFriendlyName", acCfg.acFriendlyName);
                acCfg.bus = parseBus(jModule);

                cJSON* jRemote = cJSON_GetObjectItem(jModule, "remote");
                if (cJSON_IsObject(jRemote)) {
                    acCfg.remote.manifestUrl = getString(jRemote, "manifestUrl");
                    acCfg.remote.profileUrl = getString(jRemote, "profileUrl");
                    acCfg.remote.profileVersion = getString(jRemote, "profileVersion");
                    acCfg.remote.checksum = getString(jRemote, "checksum");
                }

                cfg.moduleConfig = acCfg;
            }
        }
    }

    cJSON_Delete(root);
    std::string reason;
    cfg.isValid = cfg.isValid && validate(cfg, &reason);
    if (!cfg.isValid && !reason.empty()) {
        ESP_LOGW(TAG, "Invalid config: %s", reason.c_str());
    }
    return cfg;
}

bool ConfigManager::save(const DeviceConfig& cfg) {
    if (!init()) return false;

    cJSON* root = cJSON_CreateObject();

    cJSON_AddNumberToObject(root, "configVersion", cfg.configVersion);
    cJSON_AddBoolToObject(root, "isValid", cfg.isValid);
    cJSON_AddStringToObject(root, "friendlyName", cfg.friendlyName.c_str());
    cJSON_AddStringToObject(root, "selectedProductId", cfg.selectedProductId.c_str());

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
            cJSON_AddStringToObject(jModule, "productId", mcfg.productId.c_str());
            cJSON_AddStringToObject(jModule, "acProfilePath", mcfg.acProfilePath.c_str());
            cJSON_AddStringToObject(jModule, "acFriendlyName", mcfg.acFriendlyName.c_str());
            addBusJson(jModule, mcfg.bus);

            cJSON* jRemote = cJSON_CreateObject();
            cJSON_AddStringToObject(jRemote, "manifestUrl", mcfg.remote.manifestUrl.c_str());
            cJSON_AddStringToObject(jRemote, "profileUrl", mcfg.remote.profileUrl.c_str());
            cJSON_AddStringToObject(jRemote, "profileVersion", mcfg.remote.profileVersion.c_str());
            cJSON_AddStringToObject(jRemote, "checksum", mcfg.remote.checksum.c_str());
            cJSON_AddItemToObject(jModule, "remote", jRemote);
        }

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

    if (!writeTextFile(kConfigPath, json)) {
        ESP_LOGE(TAG, "Failed to write config JSON to %s", kConfigPath);
        return false;
    }

    ESP_LOGI(TAG, "Configuration saved to %s", kConfigPath);
    return true;
}

bool ConfigManager::validate(const DeviceConfig& cfg, std::string* reason) {
    if (cfg.configVersion <= 0) {
        if (reason) *reason = "configVersion must be positive";
        return false;
    }
    if (cfg.friendlyName.empty()) {
        if (reason) *reason = "friendlyName is required";
        return false;
    }
    if (cfg.wifiSsid.empty()) {
        if (reason) *reason = "wifi.ssid is required";
        return false;
    }

    bool validModule = false;
    std::visit([&](const auto& moduleCfg) {
        using T = std::decay_t<decltype(moduleCfg)>;
        if constexpr (std::is_same_v<T, AcModuleConfig>) {
            validModule = !moduleCfg.acProfilePath.empty() && !moduleCfg.productId.empty();
            if (!validModule && reason) {
                *reason = "AC productId and acProfilePath are required";
            }
        }
    }, cfg.moduleConfig);

    return validModule;
}
