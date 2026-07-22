#include "src/Connectors/Web/RuntimeHttpConnector.hpp"

#include <cstdio>
#include <cstring>
#include <string>

#include "sdkconfig.h"

#include "src/Config/ConnectorSecretStore.hpp"
#include "src/Config/UserConfigStore.hpp"
#include "src/Connectors/Matter/MatterPlatform.hpp"
#include "src/Wifi/WiFiService.hpp"

extern "C" {
#include "cJSON.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_random.h"
#include "esp_system.h"
#include "freertos/task.h"
#include "mdns.h"
}

namespace opencomfort {
namespace {

constexpr const char* kTag = "RuntimeHttp";
constexpr size_t kMaxBody = 2048;
constexpr TickType_t kSessionDuration = pdMS_TO_TICKS(60 * 60 * 1000);
constexpr TickType_t kLoginDelay = pdMS_TO_TICKS(1000);
constexpr TickType_t kLoginLockout = pdMS_TO_TICKS(30000);

RuntimeHttpConnector* self(httpd_req_t* request)
{
    return static_cast<RuntimeHttpConnector*>(request->user_ctx);
}

const char* statusText(int status)
{
    switch (status) {
        case 200: return "200 OK";
        case 202: return "202 Accepted";
        case 400: return "400 Bad Request";
        case 401: return "401 Unauthorized";
        case 403: return "403 Forbidden";
        case 404: return "404 Not Found";
        case 409: return "409 Conflict";
        case 429: return "429 Too Many Requests";
        case 500: return "500 Internal Server Error";
        case 501: return "501 Not Implemented";
        case 503: return "503 Service Unavailable";
        default: return "500 Internal Server Error";
    }
}

esp_err_t sendJson(httpd_req_t* request, cJSON* json, int status = 200)
{
    char* text = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);
    if (text == nullptr) {
        return ESP_ERR_NO_MEM;
    }
    httpd_resp_set_status(request, statusText(status));
    httpd_resp_set_type(request, "application/json");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    const esp_err_t err = httpd_resp_send(request, text, HTTPD_RESP_USE_STRLEN);
    cJSON_free(text);
    return err;
}

esp_err_t sendError(httpd_req_t* request, int status, const char* error)
{
    cJSON* json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "error", error);
    return sendJson(request, json, status);
}

std::string readBody(httpd_req_t* request)
{
    if (request->content_len <= 0 || request->content_len > static_cast<ssize_t>(kMaxBody)) {
        return {};
    }
    std::string body(static_cast<size_t>(request->content_len), '\0');
    int received = 0;
    while (received < request->content_len) {
        const int count =
            httpd_req_recv(request, body.data() + received, request->content_len - received);
        if (count <= 0) {
            return {};
        }
        received += count;
    }
    return body;
}

esp_err_t sendFile(httpd_req_t* request, const char* path, const char* contentType)
{
    FILE* file = std::fopen(path, "rb");
    if (file == nullptr) {
        return sendError(request, 404, "not_found");
    }
    httpd_resp_set_type(request, contentType);
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    char buffer[1024];
    while (true) {
        const size_t count = std::fread(buffer, 1, sizeof(buffer), file);
        if (count > 0 && httpd_resp_send_chunk(request, buffer, count) != ESP_OK) {
            std::fclose(file);
            httpd_resp_sendstr_chunk(request, nullptr);
            return ESP_FAIL;
        }
        if (count < sizeof(buffer)) {
            break;
        }
    }
    std::fclose(file);
    return httpd_resp_send_chunk(request, nullptr, 0);
}

esp_err_t requireAuth(httpd_req_t* request, bool csrf)
{
    if (!self(request)->isAuthorized(request, csrf)) {
        return sendError(request, csrf ? 403 : 401, csrf ? "csrf_invalid" : "unauthorized");
    }
    return ESP_OK;
}

const char* modeName(AcMode mode)
{
    switch (mode) {
        case AcMode::Off: return "off";
        case AcMode::Cool: return "cool";
        case AcMode::Heat: return "heat";
        case AcMode::Dry: return "dry";
        case AcMode::Fan: return "fan";
        case AcMode::Auto: return "auto";
        default: return "unknown";
    }
}

bool parseMode(const char* value, AcMode& mode)
{
    if (value == nullptr) return false;
    if (std::strcmp(value, "off") == 0) mode = AcMode::Off;
    else if (std::strcmp(value, "cool") == 0) mode = AcMode::Cool;
    else if (std::strcmp(value, "heat") == 0) mode = AcMode::Heat;
    else if (std::strcmp(value, "dry") == 0) mode = AcMode::Dry;
    else if (std::strcmp(value, "fan") == 0) mode = AcMode::Fan;
    else if (std::strcmp(value, "auto") == 0) mode = AcMode::Auto;
    else return false;
    return true;
}

const char* fanName(AcFanSpeed fan)
{
    switch (fan) {
        case AcFanSpeed::Auto: return "auto";
        case AcFanSpeed::Low: return "low";
        case AcFanSpeed::Medium: return "medium";
        case AcFanSpeed::High: return "high";
        default: return "unknown";
    }
}

bool parseFan(const char* value, AcFanSpeed& fan)
{
    if (value == nullptr) return false;
    if (std::strcmp(value, "auto") == 0) fan = AcFanSpeed::Auto;
    else if (std::strcmp(value, "low") == 0) fan = AcFanSpeed::Low;
    else if (std::strcmp(value, "medium") == 0) fan = AcFanSpeed::Medium;
    else if (std::strcmp(value, "high") == 0) fan = AcFanSpeed::High;
    else return false;
    return true;
}

esp_err_t handleRoot(httpd_req_t* request)
{
    return sendFile(request, "/spiffs/www/runtime.html", "text/html");
}

esp_err_t handleCss(httpd_req_t* request)
{
    return sendFile(request, "/spiffs/www/css/app.css", "text/css");
}

esp_err_t handleRuntimeJs(httpd_req_t* request)
{
    return sendFile(request, "/spiffs/www/js/runtime.js", "application/javascript");
}

esp_err_t handleSession(httpd_req_t* request)
{
    RuntimeHttpConnector* connector = self(request);
    cJSON* json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "mode", "runtime");
    cJSON_AddBoolToObject(json, "authenticated", connector->isAuthorized(request, false));
    cJSON_AddBoolToObject(json, "adminConfigured",
                         ConnectorSecretStore::adminPasswordConfigured());
    return sendJson(request, json);
}

esp_err_t handleLogin(httpd_req_t* request)
{
    const std::string body = readBody(request);
    cJSON* root = cJSON_Parse(body.c_str());
    if (root == nullptr) {
        return sendError(request, 400, "invalid_json");
    }
    const cJSON* password = cJSON_GetObjectItem(root, "password");
    const bool valid = cJSON_IsString(password) && password->valuestring != nullptr &&
                       self(request)->authenticate(password->valuestring);
    cJSON_Delete(root);
    if (!valid) {
        return sendError(request, 401, "invalid_credentials");
    }

    char cookie[96];
    std::snprintf(cookie, sizeof(cookie), "oc_session=%s; HttpOnly; SameSite=Strict; Path=/",
                  self(request)->sessionToken());
    httpd_resp_set_hdr(request, "Set-Cookie", cookie);
    cJSON* json = cJSON_CreateObject();
    cJSON_AddBoolToObject(json, "ok", true);
    cJSON_AddStringToObject(json, "csrfToken", self(request)->sessionToken());
    return sendJson(request, json);
}

esp_err_t handleLogout(httpd_req_t* request)
{
    if (requireAuth(request, true) != ESP_OK) {
        return ESP_OK;
    }
    self(request)->logout();
    httpd_resp_set_hdr(request, "Set-Cookie",
                       "oc_session=; Max-Age=0; HttpOnly; SameSite=Strict; Path=/");
    cJSON* json = cJSON_CreateObject();
    cJSON_AddBoolToObject(json, "ok", true);
    return sendJson(request, json);
}

esp_err_t handleDevice(httpd_req_t* request)
{
    if (requireAuth(request, false) != ESP_OK) return ESP_OK;
    RuntimeHttpConnector* connector = self(request);
    cJSON* json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "productId", connector->product().productId);
    cJSON_AddStringToObject(json, "friendlyName", connector->config().friendlyName);
    cJSON_AddStringToObject(json, "module", toString(connector->config().module));
    cJSON* capabilities = cJSON_AddObjectToObject(json, "capabilities");
    cJSON_AddBoolToObject(capabilities, "web", true);
    cJSON_AddBoolToObject(capabilities, "mqtt", true);
    cJSON_AddBoolToObject(capabilities, "matter", connector->product().supportsMatter);
    return sendJson(request, json);
}

esp_err_t handleStateGet(httpd_req_t* request)
{
    if (requireAuth(request, false) != ESP_OK) return ESP_OK;
    const RuntimeSnapshot snapshot = self(request)->runtime().snapshot();
    cJSON* json = cJSON_CreateObject();
    cJSON_AddBoolToObject(json, "available", snapshot.available);
    cJSON_AddNumberToObject(json, "revision", snapshot.revision);
    cJSON_AddNumberToObject(json, "lastApplyError", snapshot.lastApplyError);
    cJSON* desired = cJSON_AddObjectToObject(json, "desired");
    cJSON_AddBoolToObject(desired, "powerOn", snapshot.desired.powerOn);
    cJSON_AddStringToObject(desired, "mode", modeName(snapshot.desired.mode));
    cJSON_AddNumberToObject(desired, "targetTempC", snapshot.desired.targetTempC);
    cJSON_AddStringToObject(desired, "fan", fanName(snapshot.desired.fan));
    cJSON* reported = cJSON_AddObjectToObject(json, "reported");
    cJSON_AddBoolToObject(reported, "valid", snapshot.reported.valid);
    cJSON_AddNumberToObject(reported, "returnAirC", snapshot.reported.returnAirC);
    cJSON_AddNumberToObject(reported, "setTempC", snapshot.reported.setTempC);
    cJSON* capabilities = cJSON_AddObjectToObject(json, "capabilities");
    cJSON_AddNumberToObject(capabilities, "minTempC", snapshot.acCapabilities.minTempC);
    cJSON_AddNumberToObject(capabilities, "maxTempC", snapshot.acCapabilities.maxTempC);
    return sendJson(request, json);
}

esp_err_t handleStatePut(httpd_req_t* request)
{
    if (requireAuth(request, true) != ESP_OK) return ESP_OK;
    const std::string body = readBody(request);
    cJSON* root = cJSON_Parse(body.c_str());
    if (root == nullptr) return sendError(request, 400, "invalid_json");

    AcDesiredState desired = self(request)->runtime().snapshot().desired;
    const cJSON* power = cJSON_GetObjectItem(root, "powerOn");
    const cJSON* mode = cJSON_GetObjectItem(root, "mode");
    const cJSON* target = cJSON_GetObjectItem(root, "targetTempC");
    const cJSON* fan = cJSON_GetObjectItem(root, "fan");
    bool valid = true;
    if (cJSON_IsBool(power)) desired.powerOn = cJSON_IsTrue(power);
    if (cJSON_IsString(mode)) valid = parseMode(mode->valuestring, desired.mode);
    if (cJSON_IsNumber(target)) desired.targetTempC = static_cast<float>(target->valuedouble);
    if (cJSON_IsString(fan)) valid = valid && parseFan(fan->valuestring, desired.fan);
    cJSON_Delete(root);
    if (!valid) return sendError(request, 400, "invalid_state");

    uint32_t requestId = 0;
    const RuntimeRequestStatus result = self(request)->runtime().requestDesiredState(
        desired, RuntimeCommandSource::LocalWeb, &requestId);
    if (result != RuntimeRequestStatus::Accepted) {
        return sendError(request, result == RuntimeRequestStatus::QueueFull ? 503 : 400,
                         toString(result));
    }
    cJSON* json = cJSON_CreateObject();
    cJSON_AddBoolToObject(json, "accepted", true);
    cJSON_AddNumberToObject(json, "requestId", requestId);
    return sendJson(request, json, 202);
}

esp_err_t handleConnectorsGet(httpd_req_t* request)
{
    if (requireAuth(request, false) != ESP_OK) return ESP_OK;
    const ConnectorConfig& config = self(request)->config().connectors;
    cJSON* json = cJSON_CreateObject();
    cJSON_AddBoolToObject(json, "localWebEnabled", true);
    cJSON_AddBoolToObject(json, "mqttEnabled", config.mqttEnabled);
    cJSON_AddStringToObject(json, "mqttHost", config.mqttHost);
    cJSON_AddNumberToObject(json, "mqttPort", config.mqttPort);
    cJSON_AddBoolToObject(json, "mqttTls", config.mqttTls);
    MqttCredentials credentials;
    cJSON_AddBoolToObject(json, "mqttCredentialsConfigured",
                         ConnectorSecretStore::loadMqttCredentials(credentials) == ESP_OK);
    cJSON_AddBoolToObject(json, "matterSupported", self(request)->product().supportsMatter);
    cJSON_AddBoolToObject(json, "matterEnabled", config.matterEnabled);
    cJSON_AddBoolToObject(json, "matterRunning", MatterPlatform::isRunning());
    return sendJson(request, json);
}

esp_err_t handleConnectorsPut(httpd_req_t* request)
{
    if (requireAuth(request, true) != ESP_OK) return ESP_OK;
    const std::string body = readBody(request);
    cJSON* root = cJSON_Parse(body.c_str());
    if (root == nullptr) return sendError(request, 400, "invalid_json");

    UserConfig config = self(request)->config();
    const cJSON* mqttEnabled = cJSON_GetObjectItem(root, "mqttEnabled");
    const cJSON* mqttHost = cJSON_GetObjectItem(root, "mqttHost");
    const cJSON* mqttPort = cJSON_GetObjectItem(root, "mqttPort");
    const cJSON* mqttTls = cJSON_GetObjectItem(root, "mqttTls");
    const cJSON* matterEnabled = cJSON_GetObjectItem(root, "matterEnabled");
    const cJSON* friendlyName = cJSON_GetObjectItem(root, "friendlyName");
    const cJSON* mqttUsername = cJSON_GetObjectItem(root, "mqttUsername");
    const cJSON* mqttPassword = cJSON_GetObjectItem(root, "mqttPassword");
    const cJSON* adminPassword = cJSON_GetObjectItem(root, "adminPassword");

    if (cJSON_IsBool(mqttEnabled)) config.connectors.mqttEnabled = cJSON_IsTrue(mqttEnabled);
    if (cJSON_IsString(mqttHost)) {
        copyBounded(config.connectors.mqttHost, sizeof(config.connectors.mqttHost),
                    mqttHost->valuestring);
    }
    if (cJSON_IsNumber(mqttPort) && mqttPort->valueint > 0 && mqttPort->valueint <= 65535) {
        config.connectors.mqttPort = static_cast<uint16_t>(mqttPort->valueint);
    }
    if (cJSON_IsBool(mqttTls)) config.connectors.mqttTls = cJSON_IsTrue(mqttTls);
    if (cJSON_IsBool(matterEnabled)) config.connectors.matterEnabled = cJSON_IsTrue(matterEnabled);
    if (cJSON_IsString(friendlyName)) {
        copyBounded(config.friendlyName, sizeof(config.friendlyName), friendlyName->valuestring);
    }

    if (UserConfigStore::validate(config, self(request)->product()) != ConfigStatus::Ok) {
        cJSON_Delete(root);
        return sendError(request, 400, "invalid_connector_config");
    }

    if (cJSON_IsString(mqttUsername) || cJSON_IsString(mqttPassword)) {
        MqttCredentials credentials;
        (void)ConnectorSecretStore::loadMqttCredentials(credentials);
        if (cJSON_IsString(mqttUsername)) {
            copyBounded(credentials.username, sizeof(credentials.username),
                        mqttUsername->valuestring);
        }
        if (cJSON_IsString(mqttPassword)) {
            copyBounded(credentials.password, sizeof(credentials.password),
                        mqttPassword->valuestring);
        }
        if (ConnectorSecretStore::saveMqttCredentials(credentials) != ESP_OK) {
            cJSON_Delete(root);
            return sendError(request, 500, "secret_save_failed");
        }
    }
    if (cJSON_IsString(adminPassword) && adminPassword->valuestring[0] != '\0' &&
        ConnectorSecretStore::setAdminPassword(adminPassword->valuestring) != ESP_OK) {
        cJSON_Delete(root);
        return sendError(request, 400, "invalid_admin_password");
    }
    cJSON_Delete(root);

    if (UserConfigStore::save(config) != ConfigStatus::Ok) {
        return sendError(request, 500, "config_save_failed");
    }
    self(request)->setConfig(config);
    self(request)->scheduleRestart();
    cJSON* json = cJSON_CreateObject();
    cJSON_AddBoolToObject(json, "ok", true);
    cJSON_AddBoolToObject(json, "rebootScheduled", true);
    return sendJson(request, json);
}

esp_err_t handleMatterCommission(httpd_req_t* request)
{
    if (requireAuth(request, true) != ESP_OK) return ESP_OK;
    if (!self(request)->product().supportsMatter || !self(request)->config().connectors.matterEnabled) {
        return sendError(request, 400, "matter_not_enabled");
    }
    const esp_err_t err = MatterPlatform::openCommissioningWindow();
    if (err != ESP_OK) {
        return sendError(request, 503, "commissioning_unavailable");
    }
    cJSON* json = cJSON_CreateObject();
    cJSON_AddBoolToObject(json, "ok", true);
    return sendJson(request, json);
}

esp_err_t handleWifiPut(httpd_req_t* request)
{
    if (requireAuth(request, true) != ESP_OK) return ESP_OK;
    const std::string body = readBody(request);
    cJSON* root = cJSON_Parse(body.c_str());
    if (root == nullptr) return sendError(request, 400, "invalid_json");
    const cJSON* ssid = cJSON_GetObjectItem(root, "ssid");
    const cJSON* password = cJSON_GetObjectItem(root, "password");
    if (!cJSON_IsString(ssid) || ssid->valuestring[0] == '\0') {
        cJSON_Delete(root);
        return sendError(request, 400, "ssid_required");
    }
    WifiCredentials credentials;
    copyBounded(credentials.ssid, sizeof(credentials.ssid), ssid->valuestring);
    if (cJSON_IsString(password)) {
        copyBounded(credentials.password, sizeof(credentials.password), password->valuestring);
    }
    cJSON_Delete(root);
    if (WiFiService::saveCredentials(credentials) != ESP_OK) {
        return sendError(request, 500, "wifi_save_failed");
    }
    self(request)->scheduleRestart();
    cJSON* json = cJSON_CreateObject();
    cJSON_AddBoolToObject(json, "ok", true);
    cJSON_AddBoolToObject(json, "rebootScheduled", true);
    return sendJson(request, json);
}

esp_err_t registerRoute(httpd_handle_t server, const char* uri, httpd_method_t method,
                        esp_err_t (*handler)(httpd_req_t*), void* context)
{
    const httpd_uri_t route = {uri, method, handler, context};
    return httpd_register_uri_handler(server, &route);
}

}  // namespace

RuntimeHttpConnector::RuntimeHttpConnector(RuntimeController& runtime,
                                           const ProductDefinition& product)
    : runtime_(runtime), product_(product)
{
}

RuntimeHttpConnector::~RuntimeHttpConnector()
{
    stop();
}

const char* RuntimeHttpConnector::id() const
{
    return "web";
}

bool RuntimeHttpConnector::enabled(const ConnectorConfig&) const
{
    return true;
}

esp_err_t RuntimeHttpConnector::start(const UserConfig& config)
{
    if (server_ != nullptr) return ESP_OK;
    config_ = config;
    httpd_config_t httpConfig = HTTPD_DEFAULT_CONFIG();
    httpConfig.server_port = CONFIG_OPENCOMFORT_RUNTIME_HTTP_PORT;
    httpConfig.max_uri_handlers = 16;
    httpConfig.stack_size = 8192;
    esp_err_t err = httpd_start(&server_, &httpConfig);
    if (err != ESP_OK) {
        status_ = {"web", ConnectorState::Degraded, err, 0};
        return err;
    }

    err = registerRoute(server_, "/", HTTP_GET, handleRoot, this);
    if (err == ESP_OK) err = registerRoute(server_, "/css/app.css", HTTP_GET, handleCss, this);
    if (err == ESP_OK) err = registerRoute(server_, "/js/runtime.js", HTTP_GET, handleRuntimeJs, this);
    if (err == ESP_OK) err = registerRoute(server_, "/api/v1/session", HTTP_GET, handleSession, this);
    if (err == ESP_OK) err = registerRoute(server_, "/api/v1/auth/login", HTTP_POST, handleLogin, this);
    if (err == ESP_OK) err = registerRoute(server_, "/api/v1/auth/logout", HTTP_POST, handleLogout, this);
    if (err == ESP_OK) err = registerRoute(server_, "/api/v1/device", HTTP_GET, handleDevice, this);
    if (err == ESP_OK) err = registerRoute(server_, "/api/v1/state", HTTP_GET, handleStateGet, this);
    if (err == ESP_OK) err = registerRoute(server_, "/api/v1/state", HTTP_PUT, handleStatePut, this);
    if (err == ESP_OK) {
        err = registerRoute(server_, "/api/v1/connectors", HTTP_GET, handleConnectorsGet, this);
    }
    if (err == ESP_OK) {
        err = registerRoute(server_, "/api/v1/connectors", HTTP_PUT, handleConnectorsPut, this);
    }
    if (err == ESP_OK) {
        err = registerRoute(server_, "/api/v1/matter/commissioning-window", HTTP_POST,
                            handleMatterCommission, this);
    }
    if (err == ESP_OK) err = registerRoute(server_, "/api/v1/wifi", HTTP_PUT, handleWifiPut, this);
    if (err != ESP_OK) {
        stop();
        status_ = {"web", ConnectorState::Degraded, err, 0};
        return err;
    }

    (void)startMdns();
    status_ = {"web", ConnectorState::Running, ESP_OK, 0};
    ESP_LOGI(kTag, "Runtime portal started");
    return ESP_OK;
}

void RuntimeHttpConnector::stop()
{
    if (server_ != nullptr) {
        httpd_stop(server_);
        server_ = nullptr;
    }
    stopMdns();
    status_.state = ConnectorState::Stopped;
}

esp_err_t RuntimeHttpConnector::reconfigure(const UserConfig& config)
{
    config_ = config;
    return server_ == nullptr ? start(config) : ESP_OK;
}

void RuntimeHttpConnector::tick()
{
    if (restartAt_ != 0 &&
        static_cast<int32_t>(xTaskGetTickCount() - restartAt_) >= 0) {
        esp_restart();
    }
}

ConnectorStatus RuntimeHttpConnector::status() const
{
    return status_;
}

RuntimeController& RuntimeHttpConnector::runtime()
{
    return runtime_;
}

const ProductDefinition& RuntimeHttpConnector::product() const
{
    return product_;
}

const UserConfig& RuntimeHttpConnector::config() const
{
    return config_;
}

void RuntimeHttpConnector::setConfig(const UserConfig& config)
{
    config_ = config;
}

bool RuntimeHttpConnector::authenticate(const char* password)
{
    const TickType_t now = xTaskGetTickCount();
    if (static_cast<int32_t>(now - nextLoginAllowedAt_) < 0) {
        return false;
    }
    if (!ConnectorSecretStore::verifyAdminPassword(password)) {
        failedLoginAttempts_++;
        nextLoginAllowedAt_ = now + (failedLoginAttempts_ >= 5 ? kLoginLockout : kLoginDelay);
        return false;
    }

    failedLoginAttempts_ = 0;
    nextLoginAllowedAt_ = 0;
    uint8_t random[16];
    esp_fill_random(random, sizeof(random));
    for (size_t i = 0; i < sizeof(random); ++i) {
        std::snprintf(sessionToken_ + (i * 2), 3, "%02x", random[i]);
    }
    sessionExpiresAt_ = now + kSessionDuration;
    return true;
}

bool RuntimeHttpConnector::isAuthorized(httpd_req_t* request, bool requireCsrf) const
{
    if (sessionToken_[0] == '\0' ||
        static_cast<int32_t>(xTaskGetTickCount() - sessionExpiresAt_) >= 0) {
        return false;
    }
    char cookie[256] = {};
    if (httpd_req_get_hdr_value_str(request, "Cookie", cookie, sizeof(cookie)) != ESP_OK) {
        return false;
    }
    char expected[48];
    std::snprintf(expected, sizeof(expected), "oc_session=%s", sessionToken_);
    if (std::strstr(cookie, expected) == nullptr) {
        return false;
    }
    if (!requireCsrf) {
        return true;
    }
    char csrf[40] = {};
    return httpd_req_get_hdr_value_str(request, "X-OC-CSRF", csrf, sizeof(csrf)) == ESP_OK &&
           std::strcmp(csrf, sessionToken_) == 0;
}

const char* RuntimeHttpConnector::sessionToken() const
{
    return sessionToken_;
}

void RuntimeHttpConnector::logout()
{
    std::memset(sessionToken_, 0, sizeof(sessionToken_));
    sessionExpiresAt_ = 0;
}

void RuntimeHttpConnector::scheduleRestart()
{
    restartAt_ = xTaskGetTickCount() + pdMS_TO_TICKS(1000);
}

bool RuntimeHttpConnector::startMdns()
{
    if (mdnsStarted_) return true;
    if (mdns_init() != ESP_OK) return false;
    uint8_t mac[6] = {};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    char hostname[32];
    std::snprintf(hostname, sizeof(hostname), "opencomfort-%02x%02x", mac[4], mac[5]);
    if (mdns_hostname_set(hostname) != ESP_OK ||
        mdns_instance_name_set(config_.friendlyName) != ESP_OK ||
        mdns_service_add(nullptr, "_http", "_tcp", CONFIG_OPENCOMFORT_RUNTIME_HTTP_PORT, nullptr,
                         0) != ESP_OK) {
        mdns_free();
        return false;
    }
    mdnsStarted_ = true;
    return true;
}

void RuntimeHttpConnector::stopMdns()
{
    if (mdnsStarted_) {
        mdns_free();
        mdnsStarted_ = false;
    }
}

}  // namespace opencomfort
