#include "src/Http/CaptivePortal.hpp"

extern "C" {
    #include "cJSON.h"
    #include "esp_log.h"
    #include "freertos/FreeRTOS.h"
    #include "freertos/task.h"
}

#include <string>
#include <type_traits>

#include "src/Configs/ConfigManager.hpp"
#include "src/Configs/RemoteConfigProvider.hpp"
#include "src/Core/WifiManager.hpp"
#include "src/Http/HttpBody.hpp"
#include "src/Http/HttpPages.hpp"

static const char* TAG = "CaptivePortal";
static CaptivePortal* s_activePortal = nullptr;

static std::string jsonString(cJSON* object, const char* key, const std::string& fallback = {}) {
    cJSON* item = cJSON_GetObjectItem(object, key);
    return cJSON_IsString(item) ? item->valuestring : fallback;
}

static int jsonInt(cJSON* object, const char* key, int fallback) {
    cJSON* item = cJSON_GetObjectItem(object, key);
    return cJSON_IsNumber(item) ? item->valueint : fallback;
}

static Bus parseSetupBus(cJSON* root) {
    cJSON* bus = cJSON_GetObjectItem(root, "bus");
    if (!cJSON_IsObject(bus)) return SpiConfig{};

    const std::string type = jsonString(bus, "type", "spi");
    if (type == "uart" || type == "rs485") {
        UartConfig cfg;
        cfg.port = jsonInt(bus, "port", cfg.port);
        cfg.txPin = jsonInt(bus, "txPin", cfg.txPin);
        cfg.rxPin = jsonInt(bus, "rxPin", cfg.rxPin);
        cfg.baudRate = jsonInt(bus, "baudRate", cfg.baudRate);
        return cfg;
    }
    if (type == "can") {
        CanConfig cfg;
        cfg.txPin = jsonInt(bus, "txPin", cfg.txPin);
        cfg.rxPin = jsonInt(bus, "rxPin", cfg.rxPin);
        cfg.baudRate = jsonInt(bus, "baudRate", cfg.baudRate);
        cfg.id = static_cast<uint32_t>(jsonInt(bus, "id", static_cast<int>(cfg.id)));
        return cfg;
    }

    SpiConfig cfg;
    cfg.host = jsonInt(bus, "host", cfg.host);
    cfg.sckPin = jsonInt(bus, "sckPin", cfg.sckPin);
    cfg.mosiPin = jsonInt(bus, "mosiPin", cfg.mosiPin);
    cfg.misoPin = jsonInt(bus, "misoPin", cfg.misoPin);
    cfg.csPin = jsonInt(bus, "csPin", cfg.csPin);
    cfg.mode = jsonInt(bus, "mode", cfg.mode);
    cfg.frequency = jsonInt(bus, "frequency", cfg.frequency);
    return cfg;
}

static esp_err_t sendJson(httpd_req_t* req, int statusCode, const std::string& body) {
    httpd_resp_set_type(req, "application/json");
    if (statusCode >= 400) {
        httpd_resp_set_status(req, statusCode == 400 ? "400 Bad Request" : "500 Internal Server Error");
    }
    return httpd_resp_send(req, body.c_str(), body.size());
}

DeviceConfig CaptivePortal::run() {
    finished_ = false;
    result_ = DeviceConfig{};
    s_activePortal = this;

    startSoftAp();
    startHttpServer();

    ESP_LOGI(TAG, "Waiting for setup to complete");
    while (!finished_) {
        vTaskDelay(pdMS_TO_TICKS(250));
    }

    stopHttpServer();
    s_activePortal = nullptr;
    return result_;
}

void CaptivePortal::startSoftAp() {
    WifiManager::startSetupAp("OpenComfort-Setup");
}

void CaptivePortal::startHttpServer() {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.uri_match_fn = httpd_uri_match_wildcard;

    if (httpd_start(&server_, &config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start setup HTTP server");
        return;
    }

    httpd_uri_t root = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = rootHandler,
        .user_ctx = this
    };
    httpd_register_uri_handler(server_, &root);

    httpd_uri_t setup = {
        .uri = "/api/setup",
        .method = HTTP_POST,
        .handler = setupHandler,
        .user_ctx = this
    };
    httpd_register_uri_handler(server_, &setup);

    httpd_uri_t products = {
        .uri = "/api/setup/products",
        .method = HTTP_GET,
        .handler = productsHandler,
        .user_ctx = this
    };
    httpd_register_uri_handler(server_, &products);
}

void CaptivePortal::stopHttpServer() {
    if (server_) {
        httpd_stop(server_);
        server_ = nullptr;
    }
}

esp_err_t CaptivePortal::rootHandler(httpd_req_t* req) {
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, HttpPages::kSetupPage, HTTPD_RESP_USE_STRLEN);
}

esp_err_t CaptivePortal::productsHandler(httpd_req_t* req) {
    RemoteConfigProvider provider;
    std::vector<ProductInfo> products;
    std::string error;
    if (!provider.listProducts(products, &error)) {
        return sendJson(req, 500, "{\"error\":\"" + error + "\"}");
    }

    cJSON* root = cJSON_CreateObject();
    cJSON* items = cJSON_CreateArray();
    for (const auto& product : products) {
        cJSON* item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "id", product.id.c_str());
        cJSON_AddStringToObject(item, "name", product.name.c_str());
        cJSON_AddStringToObject(item, "module", product.module.c_str());
        cJSON_AddStringToObject(item, "bus", product.bus.c_str());
        if (!product.version.empty()) {
            cJSON_AddStringToObject(item, "version", product.version.c_str());
        }
        cJSON_AddItemToArray(items, item);
    }
    cJSON_AddItemToObject(root, "products", items);

    char* json = cJSON_PrintUnformatted(root);
    const std::string body = json ? json : "{\"products\":[]}";
    cJSON_free(json);
    cJSON_Delete(root);
    return sendJson(req, 200, body);
}

esp_err_t CaptivePortal::setupHandler(httpd_req_t* req) {
    auto* portal = static_cast<CaptivePortal*>(req->user_ctx ? req->user_ctx : s_activePortal);
    if (!portal) return ESP_FAIL;

    std::string body;
    if (!HttpBody::read(req, body, 2048)) {
        return sendJson(req, 400, "{\"error\":\"invalid request size or failed to read body\"}");
    }

    cJSON* root = cJSON_Parse(body.c_str());
    if (!root) {
        return sendJson(req, 400, "{\"error\":\"invalid JSON\"}");
    }

    const std::string wifiSsid = jsonString(root, "wifiSsid");
    const std::string wifiPassword = jsonString(root, "wifiPassword");
    const std::string friendlyName = jsonString(root, "friendlyName", "OpenComfort Device");
    const std::string productId = jsonString(root, "productId", "mitsubishi_heavy_industries");
    const std::string manifestUrl = jsonString(root, "manifestUrl");

    if (wifiSsid.empty() || productId.empty()) {
        cJSON_Delete(root);
        return sendJson(req, 400, "{\"error\":\"wifiSsid and productId are required\"}");
    }

    if (!WifiManager::connectStation(wifiSsid, wifiPassword, 20000)) {
        cJSON_Delete(root);
        return sendJson(req, 400, "{\"error\":\"failed to connect to Wi-Fi\"}");
    }

    RemoteConfigProvider provider(manifestUrl.empty()
        ? RemoteConfigProvider::kDefaultManifestUrl
        : manifestUrl);
    RemoteProfileResult remote = provider.fetchAndCacheProfile(productId);
    if (!remote.success) {
        std::string response = "{\"error\":\"" + remote.error + "\"}";
        cJSON_Delete(root);
        return sendJson(req, 400, response);
    }

    AcModuleConfig acCfg;
    acCfg.productId = productId;
    acCfg.acFriendlyName = jsonString(root, "acFriendlyName", "Air Conditioner");
    acCfg.acProfilePath = remote.cachedPath;
    acCfg.bus = parseSetupBus(root);
    acCfg.remote.manifestUrl = manifestUrl;
    acCfg.remote.profileUrl = remote.profileUrl;
    acCfg.remote.profileVersion = remote.profileVersion;
    acCfg.remote.checksum = remote.checksum;

    DeviceConfig cfg;
    cfg.configVersion = 1;
    cfg.friendlyName = friendlyName;
    cfg.wifiSsid = wifiSsid;
    cfg.wifiPassword = wifiPassword;
    cfg.selectedProductId = productId;
    cfg.moduleConfig = acCfg;
    cfg.isValid = true;

    std::string validationError;
    if (!ConfigManager::validate(cfg, &validationError)) {
        std::string response = "{\"error\":\"" + validationError + "\"}";
        cJSON_Delete(root);
        return sendJson(req, 400, response);
    }

    if (!ConfigManager::save(cfg)) {
        cJSON_Delete(root);
        return sendJson(req, 500, "{\"error\":\"failed to save config\"}");
    }

    portal->result_ = cfg;
    portal->finished_ = true;
    cJSON_Delete(root);
    return sendJson(req, 200, "{\"ok\":true}");
}
