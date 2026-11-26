#include "src/Connectors/Web/RuntimeHttpConnector.hpp"

#include <algorithm>
#include <climits>
#include <cstdio>
#include <cstring>
#include <memory>
#include <new>
#include <string>

#include "sdkconfig.h"

#include "src/Config/ConnectorSecretStore.hpp"
#include "src/Config/DeviceHostname.hpp"
#include "src/Modules/Ac/MhiLink.hpp"
#include "src/Config/UserConfigStore.hpp"
#include "src/Connectors/Matter/MatterPlatform.hpp"
#include "src/Http/WebAssets.hpp"
#include "src/Logging/LogBuffer.hpp"
#include "src/Schedule/ScheduleService.hpp"
#include "src/Update/UpdateService.hpp"
#include "src/ThirdParty/qrcodegen/qrcodegen.h"
#include "src/Wifi/WiFiService.hpp"

extern "C" {
#include "cJSON.h"
#include "esp_app_desc.h"
#include "esp_app_format.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_ota_ops.h"
#include "esp_timer.h"
#include "esp_wifi.h"
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
    return sendWebAsset(request, WebAsset::RuntimeHtml);
}

esp_err_t handleCss(httpd_req_t* request)
{
    return sendWebAsset(request, WebAsset::AppCss);
}

esp_err_t handleFont(httpd_req_t* request)
{
    return sendWebAsset(request, WebAsset::Font);
}

esp_err_t handleRuntimeJs(httpd_req_t* request)
{
    return sendWebAsset(request, WebAsset::RuntimeJs);
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
    const bool remember = cJSON_IsTrue(cJSON_GetObjectItem(root, "remember"));
    const RuntimeHttpConnector::Session* session =
        (cJSON_IsString(password) && password->valuestring != nullptr)
            ? self(request)->authenticate(password->valuestring, remember)
            : nullptr;
    cJSON_Delete(root);
    if (session == nullptr) {
        return sendError(request, 401, "invalid_credentials");
    }

    // Must outlive httpd_resp_send, which reads the header pointer.
    char cookie[128];
    std::snprintf(cookie, sizeof(cookie), "oc_session=%s; HttpOnly; SameSite=Strict; Path=/%s",
                  session->token, remember ? "; Max-Age=31536000" : "");
    httpd_resp_set_hdr(request, "Set-Cookie", cookie);
    cJSON* json = cJSON_CreateObject();
    cJSON_AddBoolToObject(json, "ok", true);
    cJSON_AddStringToObject(json, "csrfToken", session->csrf);
    return sendJson(request, json);
}

esp_err_t handleLogout(httpd_req_t* request)
{
    if (requireAuth(request, true) != ESP_OK) {
        return ESP_OK;
    }
    self(request)->logout(request);
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
    char hostname[kMaxHostnameLen + 1];
    if (resolveDeviceHostname(connector->config(), hostname, sizeof(hostname)) != ESP_OK) {
        return sendError(request, 500, "hostname_unavailable");
    }
    cJSON* json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "productId", connector->product().productId);
    cJSON_AddStringToObject(json, "friendlyName", connector->config().friendlyName);
    cJSON_AddStringToObject(json, "hostname", hostname);
    cJSON_AddStringToObject(json, "hostnameOverride", connector->config().hostname);
    cJSON_AddStringToObject(json, "module", toString(connector->config().module));
    cJSON_AddStringToObject(json, "model", connector->config().ac.modelId);
    cJSON* capabilities = cJSON_AddObjectToObject(json, "capabilities");
    cJSON_AddBoolToObject(capabilities, "web", true);
    cJSON_AddBoolToObject(capabilities, "mqtt", connector->product().supportsMqtt);
    cJSON_AddBoolToObject(capabilities, "matter", connector->product().supportsMatter);
    cJSON_AddBoolToObject(capabilities, "ota", esp_ota_get_next_update_partition(nullptr) != nullptr);
    cJSON_AddStringToObject(json, "firmwareVersion", esp_app_get_description()->version);
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
    const AcRuntimeCapabilities& caps = snapshot.acCapabilities;
    if (caps.vaneCount > 0 && snapshot.desired.vanes < caps.vaneCount) {
        cJSON_AddStringToObject(desired, "vanes", caps.vaneNames[snapshot.desired.vanes]);
    }
    const AcReportedState& r = snapshot.reported;
    cJSON* reported = cJSON_AddObjectToObject(json, "reported");
    cJSON_AddBoolToObject(reported, "valid", r.valid);
    // Each value only when the profile reads it back; the page shows what is there.
    if (r.hasRoomTemp) cJSON_AddNumberToObject(reported, "returnAirC", r.returnAirC);
    if (r.hasSetTemp) cJSON_AddNumberToObject(reported, "setTempC", r.setTempC);
    if (r.hasPower) cJSON_AddBoolToObject(reported, "powerOn", r.powerOn);
    if (r.hasMode) cJSON_AddStringToObject(reported, "mode", modeName(r.mode));
    if (r.hasFan) cJSON_AddStringToObject(reported, "fan", fanName(r.fan));
    if (r.hasVanes && r.vanes < caps.vaneCount) cJSON_AddStringToObject(reported, "vanes", caps.vaneNames[r.vanes]);
    if (r.hasCompressor) cJSON_AddBoolToObject(reported, "compressorRunning", r.compressorRunning);
    cJSON_AddStringToObject(json, "action", toString(acAction(snapshot.desired, r)));
    cJSON* capabilities = cJSON_AddObjectToObject(json, "capabilities");
    cJSON_AddNumberToObject(capabilities, "minTempC", caps.minTempC);
    cJSON_AddNumberToObject(capabilities, "maxTempC", caps.maxTempC);
    cJSON_AddBoolToObject(capabilities, "reportsAction", caps.reportsAction);
    cJSON* vanes = cJSON_AddArrayToObject(capabilities, "vanes");
    for (size_t i = 0; i < caps.vaneCount; ++i) cJSON_AddItemToArray(vanes, cJSON_CreateString(caps.vaneNames[i]));
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
    const cJSON* vanes = cJSON_GetObjectItem(root, "vanes");
    bool valid = true;
    if (cJSON_IsBool(power)) desired.powerOn = cJSON_IsTrue(power);
    if (cJSON_IsString(mode)) valid = parseMode(mode->valuestring, desired.mode);
    if (cJSON_IsNumber(target)) desired.targetTempC = static_cast<float>(target->valuedouble);
    if (cJSON_IsString(fan)) valid = valid && parseFan(fan->valuestring, desired.fan);
    if (cJSON_IsString(vanes)) {
        const AcRuntimeCapabilities caps = self(request)->runtime().snapshot().acCapabilities;
        bool found = false;
        for (size_t i = 0; i < caps.vaneCount; ++i) {
            if (std::strcmp(caps.vaneNames[i], vanes->valuestring) == 0) {
                desired.vanes = static_cast<uint8_t>(i);
                found = true;
            }
        }
        valid = valid && found;
    }
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
    cJSON_AddBoolToObject(json, "mqttSupported", self(request)->product().supportsMqtt);
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
    const cJSON* hostname = cJSON_GetObjectItem(root, "hostname");
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
    if (hostname != nullptr &&
        (!cJSON_IsString(hostname) ||
         !normalizeDeviceHostname(hostname->valuestring, config.hostname, sizeof(config.hostname)))) {
        cJSON_Delete(root);
        return sendError(request, 400, "invalid_hostname");
    }
    char resolvedHostname[kMaxHostnameLen + 1];
    if (resolveDeviceHostname(config, resolvedHostname, sizeof(resolvedHostname)) != ESP_OK) {
        cJSON_Delete(root);
        return sendError(request, 500, "hostname_unavailable");
    }

    if (UserConfigStore::validate(config, self(request)->product()) != ConfigStatus::Ok) {
        cJSON_Delete(root);
        return sendError(request, 400, "invalid_connector_config");
    }
    // One smart-home integration at a time: both would show the unit twice in Home Assistant.
    if (config.connectors.mqttEnabled && config.connectors.matterEnabled) {
        cJSON_Delete(root);
        return sendError(request, 400, "choose_one_integration");
    }
    // Check the new password up front so a bad one doesn't leave other settings half-saved.
    const bool changeAdminPassword =
        cJSON_IsString(adminPassword) && adminPassword->valuestring[0] != '\0';
    if (changeAdminPassword) {
        const size_t length = std::strlen(adminPassword->valuestring);
        if (length < 8 || length > 64) {
            cJSON_Delete(root);
            return sendError(request, 400, "invalid_admin_password");
        }
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
    if (changeAdminPassword &&
        ConnectorSecretStore::setAdminPassword(adminPassword->valuestring) != ESP_OK) {
        cJSON_Delete(root);
        return sendError(request, 500, "secret_save_failed");
    }
    cJSON_Delete(root);

    if (UserConfigStore::save(config) != ConfigStatus::Ok) {
        return sendError(request, 500, "config_save_failed");
    }
    self(request)->setConfig(config);
    self(request)->scheduleRestart();
    cJSON* json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "hostname", resolvedHostname);
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
    self(request)->markCommissioningOpened();
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

// Streams a firmware image into the inactive OTA slot. esp_ota_end() verifies the image
// (checksum, chip, and the signature when Secure Boot is on) before it can become bootable.
esp_err_t handleFirmwarePost(httpd_req_t* request)
{
    if (requireAuth(request, true) != ESP_OK) return ESP_OK;
    const esp_partition_t* target = esp_ota_get_next_update_partition(nullptr);
    if (target == nullptr) {
        return sendError(request, 501, "ota_unsupported");
    }
    if (request->content_len <= 0 || static_cast<size_t>(request->content_len) > target->size) {
        return sendError(request, 400, "firmware_size_invalid");
    }

    // Only one writer for the inactive slot: an online download or this upload.
    if (!UpdateService::instance().reserve()) return sendError(request, 409, "update_busy");
    struct Reservation {
        bool keep = false;  // after success the restart follows; no new download before it
        ~Reservation() { if (!keep) UpdateService::instance().release(); }
    } reservation;

    constexpr size_t kChunk = 4096;
    std::unique_ptr<char[]> bufferStorage(new (std::nothrow) char[kChunk]);
    if (!bufferStorage) {
        return sendError(request, 500, "out_of_memory");
    }
    char* buffer = bufferStorage.get();

    esp_ota_handle_t ota = 0;
    if (esp_ota_begin(target, OTA_WITH_SEQUENTIAL_WRITES, &ota) != ESP_OK) {
        return sendError(request, 500, "ota_begin_failed");
    }

    // Header + first segment header + app description: enough to check the project name.
    constexpr size_t kDescOffset = sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t);
    constexpr size_t kHeadSize = kDescOffset + sizeof(esp_app_desc_t);
    uint8_t head[kHeadSize];
    size_t headLen = 0;
    int remaining = request->content_len;
    int timeouts = 0;
    const char* failure = nullptr;
    while (remaining > 0) {
        const int count = httpd_req_recv(request, buffer,
                                         remaining < static_cast<int>(kChunk)
                                             ? remaining
                                             : static_cast<int>(kChunk));
        if (count == HTTPD_SOCK_ERR_TIMEOUT && ++timeouts < 3) {
            continue;
        }
        if (count <= 0) {
            failure = "firmware_upload_interrupted";
            break;
        }
        timeouts = 0;
        if (headLen < kHeadSize) {
            const size_t take = std::min(kHeadSize - headLen, static_cast<size_t>(count));
            std::memcpy(head + headLen, buffer, take);
            headLen += take;
            if (headLen == kHeadSize) {
                const auto* desc = reinterpret_cast<const esp_app_desc_t*>(head + kDescOffset);
                if (desc->magic_word != ESP_APP_DESC_MAGIC_WORD ||
                    std::strncmp(desc->project_name, esp_app_get_description()->project_name,
                                 sizeof(desc->project_name)) != 0) {
                    failure = "firmware_not_opencomfort";
                    break;
                }
            }
        }
        if (esp_ota_write(ota, buffer, static_cast<size_t>(count)) != ESP_OK) {
            failure = "ota_write_failed";
            break;
        }
        remaining -= count;
    }

    if (failure == nullptr && headLen < kHeadSize) {
        failure = "firmware_not_opencomfort";
    }
    if (failure != nullptr) {
        esp_ota_abort(ota);
        return sendError(request, 400, failure);
    }
    if (esp_ota_end(ota) != ESP_OK) {
        return sendError(request, 400, "firmware_invalid");
    }
    if (esp_ota_set_boot_partition(target) != ESP_OK) {
        return sendError(request, 500, "ota_activate_failed");
    }

    reservation.keep = true;
    ESP_LOGI(kTag, "Firmware written to %s, restarting", target->label);
    self(request)->scheduleRestart();
    cJSON* json = cJSON_CreateObject();
    cJSON_AddBoolToObject(json, "ok", true);
    cJSON_AddBoolToObject(json, "rebootScheduled", true);
    return sendJson(request, json);
}

const char* connectorStateName(ConnectorState state)
{
    switch (state) {
        case ConnectorState::Running: return "running";
        case ConnectorState::Starting: return "starting";
        case ConnectorState::Degraded: return "degraded";
        case ConnectorState::Disabled: return "disabled";
        case ConnectorState::Stopped:
        default: return "stopped";
    }
}

const char* sourceName(RuntimeCommandSource source)
{
    switch (source) {
        case RuntimeCommandSource::LocalWeb: return "web";
        case RuntimeCommandSource::HomeAssistantMqtt: return "home_assistant";
        case RuntimeCommandSource::Matter: return "matter";
        case RuntimeCommandSource::Schedule: return "schedule";
        case RuntimeCommandSource::Internal:
        default: return "device";
    }
}

esp_err_t handleStatus(httpd_req_t* request)
{
    if (requireAuth(request, false) != ESP_OK) return ESP_OK;
    RuntimeHttpConnector* connector = self(request);
    cJSON* json = cJSON_CreateObject();

    cJSON* wifi = cJSON_AddObjectToObject(json, "wifi");
    wifi_ap_record_t ap{};
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        cJSON_AddStringToObject(wifi, "ssid", reinterpret_cast<const char*>(ap.ssid));
        cJSON_AddNumberToObject(wifi, "rssi", ap.rssi);
    }
    esp_netif_ip_info_t ip{};
    esp_netif_t* sta = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (sta != nullptr && esp_netif_get_ip_info(sta, &ip) == ESP_OK) {
        char address[16];
        std::snprintf(address, sizeof(address), IPSTR, IP2STR(&ip.ip));
        cJSON_AddStringToObject(wifi, "ip", address);
    }
    char hostname[kMaxHostnameLen + 1];
    if (resolveDeviceHostname(connector->config(), hostname, sizeof(hostname)) != ESP_OK) {
        cJSON_Delete(json);
        return sendError(request, 500, "hostname_unavailable");
    }
    char localAddress[kMaxHostnameLen + 7];
    std::snprintf(localAddress, sizeof(localAddress), "%s.local", hostname);
    cJSON_AddStringToObject(wifi, "hostname", localAddress);

    cJSON* connectors = cJSON_AddArrayToObject(json, "connectors");
    if (const ConnectorManager* manager = connector->connectorManager()) {
        ConnectorStatus statuses[6];
        const size_t count = manager->status(statuses, 6);
        for (size_t i = 0; i < count; ++i) {
            cJSON* item = cJSON_CreateObject();
            cJSON_AddStringToObject(item, "id", statuses[i].id);
            cJSON_AddStringToObject(item, "state", connectorStateName(statuses[i].state));
            cJSON_AddStringToObject(item, "lastError", esp_err_to_name(statuses[i].lastError));
            cJSON_AddItemToArray(connectors, item);
        }
    }

    const RuntimeSnapshot snapshot = connector->runtime().snapshot();
    cJSON* ac = cJSON_AddObjectToObject(json, "ac");
    cJSON_AddBoolToObject(ac, "available", snapshot.available);
    if (std::strcmp(connector->config().ac.profileId, "mitsubishi_heavy_industries") == 0 &&
        connector->product().board != nullptr) {
        cJSON_AddNumberToObject(ac, "clockBursts", mhi::observedClockBursts());
        cJSON_AddNumberToObject(ac, "sckPin", connector->product().board->mhi.sckPin);
    }
    cJSON_AddBoolToObject(ac, "answering", snapshot.reported.valid);
    cJSON_AddNumberToObject(ac, "lastRequestId", snapshot.lastRequestId);
    cJSON_AddStringToObject(ac, "lastApplyError", esp_err_to_name(snapshot.lastApplyError));
    cJSON_AddStringToObject(ac, "lastCommandSource", sourceName(snapshot.lastCommandSource));
    cJSON_AddStringToObject(ac, "bus", toString(connector->config().ac.bus));

    cJSON* device = cJSON_AddObjectToObject(json, "device");
    cJSON_AddNumberToObject(device, "uptimeS", static_cast<double>(esp_timer_get_time() / 1000000));
    cJSON_AddNumberToObject(device, "freeHeap", esp_get_free_heap_size());
    cJSON_AddStringToObject(device, "firmwareVersion", esp_app_get_description()->version);
    return sendJson(request, json);
}

// Streamed: 1440 samples as cJSON nodes would need tens of KB of heap.
esp_err_t handleHistory(httpd_req_t* request)
{
    if (requireAuth(request, false) != ESP_OK) return ESP_OK;
    std::unique_ptr<int16_t[]> samples(new (std::nothrow) int16_t[RuntimeHttpConnector::kHistoryLength]);
    if (!samples) return sendError(request, 500, "out_of_memory");
    const size_t count =
        self(request)->copyHistory(samples.get(), RuntimeHttpConnector::kHistoryLength);

    httpd_resp_set_type(request, "application/json");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    char chunk[512];
    int used = std::snprintf(chunk, sizeof(chunk), "{\"intervalS\":60,\"samples\":[");
    for (size_t i = 0; i < count; ++i) {
        char value[12];
        const int n = samples[i] == INT16_MIN
                          ? std::snprintf(value, sizeof(value), "%snull", i ? "," : "")
                          : std::snprintf(value, sizeof(value), "%s%.1f", i ? "," : "",
                                          samples[i] / 10.0);
        if (used + n >= static_cast<int>(sizeof(chunk))) {
            if (httpd_resp_send_chunk(request, chunk, used) != ESP_OK) return ESP_FAIL;
            used = 0;
        }
        std::memcpy(chunk + used, value, static_cast<size_t>(n));
        used += n;
    }
    used += std::snprintf(chunk + used, sizeof(chunk) - static_cast<size_t>(used), "]}");
    if (httpd_resp_send_chunk(request, chunk, used) != ESP_OK) return ESP_FAIL;
    return httpd_resp_send_chunk(request, nullptr, 0);
}

void formatTime(char* out, size_t size, uint8_t hour, uint8_t minute)
{
    std::snprintf(out, size, "%02u:%02u", static_cast<unsigned>(hour % 24u), static_cast<unsigned>(minute % 60u));
}

esp_err_t sendSchedule(httpd_req_t* request)
{
    ScheduleService& schedule = ScheduleService::instance();
    const ScheduleConfig config = schedule.config();
    cJSON* json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "timezone", config.timezone);
    cJSON_AddBoolToObject(json, "timeValid", schedule.timeValid());
    if (schedule.timeValid()) {
        const std::time_t now = std::time(nullptr);
        std::tm local{};
        localtime_r(&now, &local);
        char time[8];
        formatTime(time, sizeof(time), static_cast<uint8_t>(local.tm_hour),
                   static_cast<uint8_t>(local.tm_min));
        cJSON_AddStringToObject(json, "now", time);
        cJSON_AddNumberToObject(json, "weekday", (local.tm_wday + 6) % 7);
    }
    cJSON_AddNumberToObject(json, "sleepSecondsLeft", schedule.sleepSecondsLeft());
    cJSON* rules = cJSON_AddArrayToObject(json, "rules");
    for (size_t i = 0; i < config.ruleCount; ++i) {
        const ScheduleRule& rule = config.rules[i];
        cJSON* item = cJSON_CreateObject();
        char time[8];
        formatTime(time, sizeof(time), rule.hour, rule.minute);
        cJSON_AddBoolToObject(item, "enabled", rule.enabled);
        cJSON_AddStringToObject(item, "time", time);
        cJSON_AddNumberToObject(item, "days", rule.days);
        cJSON_AddStringToObject(item, "action", rule.action == ScheduleAction::TurnOff ? "off" : "on");
        cJSON_AddStringToObject(item, "mode", modeName(rule.mode));
        cJSON_AddNumberToObject(item, "targetTempC", rule.targetTempC);
        cJSON_AddItemToArray(rules, item);
    }
    return sendJson(request, json);
}

esp_err_t handleScheduleGet(httpd_req_t* request)
{
    if (requireAuth(request, false) != ESP_OK) return ESP_OK;
    return sendSchedule(request);
}

esp_err_t handleSchedulePut(httpd_req_t* request)
{
    if (requireAuth(request, true) != ESP_OK) return ESP_OK;
    const std::string body = readBody(request);
    cJSON* root = cJSON_Parse(body.c_str());
    if (root == nullptr) return sendError(request, 400, "invalid_json");

    ScheduleConfig config = ScheduleService::instance().config();
    bool valid = true;
    const cJSON* timezone = cJSON_GetObjectItem(root, "timezone");
    if (cJSON_IsString(timezone)) {
        valid = std::strlen(timezone->valuestring) <= kMaxTimezoneLen;
        if (valid) copyBounded(config.timezone, sizeof(config.timezone), timezone->valuestring);
    }
    const cJSON* rules = cJSON_GetObjectItem(root, "rules");
    if (valid && cJSON_IsArray(rules)) {
        const int count = cJSON_GetArraySize(rules);
        valid = count <= static_cast<int>(kMaxScheduleRules);
        config.ruleCount = 0;
        const cJSON* item = nullptr;
        cJSON_ArrayForEach(item, rules)
        {
            if (!valid) break;
            ScheduleRule rule;
            const cJSON* enabled = cJSON_GetObjectItem(item, "enabled");
            const cJSON* time = cJSON_GetObjectItem(item, "time");
            const cJSON* days = cJSON_GetObjectItem(item, "days");
            const cJSON* action = cJSON_GetObjectItem(item, "action");
            const cJSON* mode = cJSON_GetObjectItem(item, "mode");
            const cJSON* target = cJSON_GetObjectItem(item, "targetTempC");
            unsigned hour = 0, minute = 0;
            valid = cJSON_IsString(time) &&
                    std::sscanf(time->valuestring, "%2u:%2u", &hour, &minute) == 2 &&
                    cJSON_IsNumber(days) && cJSON_IsString(action);
            if (!valid) break;
            rule.enabled = !cJSON_IsBool(enabled) || cJSON_IsTrue(enabled);
            rule.hour = static_cast<uint8_t>(hour);
            rule.minute = static_cast<uint8_t>(minute);
            rule.days = static_cast<uint8_t>(days->valueint & 0x7F);
            rule.action = std::strcmp(action->valuestring, "off") == 0 ? ScheduleAction::TurnOff
                                                                       : ScheduleAction::TurnOn;
            if (cJSON_IsString(mode)) valid = parseMode(mode->valuestring, rule.mode);
            if (cJSON_IsNumber(target)) rule.targetTempC = static_cast<int8_t>(target->valueint);
            config.rules[config.ruleCount++] = rule;
        }
    }
    cJSON_Delete(root);
    if (!valid || ScheduleService::instance().setConfig(config) != ESP_OK) {
        return sendError(request, 400, "invalid_schedule");
    }
    return sendSchedule(request);
}

esp_err_t handleSleepPost(httpd_req_t* request)
{
    if (requireAuth(request, true) != ESP_OK) return ESP_OK;
    const std::string body = readBody(request);
    cJSON* root = cJSON_Parse(body.c_str());
    if (root == nullptr) return sendError(request, 400, "invalid_json");
    const cJSON* minutes = cJSON_GetObjectItem(root, "minutes");
    const bool valid = cJSON_IsNumber(minutes) && minutes->valueint >= 0 && minutes->valueint <= 720;
    if (valid) ScheduleService::instance().setSleepMinutes(static_cast<uint32_t>(minutes->valueint));
    cJSON_Delete(root);
    if (!valid) return sendError(request, 400, "invalid_sleep_timer");
    return sendSchedule(request);
}

esp_err_t handleMatterPairing(httpd_req_t* request)
{
    if (requireAuth(request, false) != ESP_OK) return ESP_OK;
    char payload[64] = {};
    char manual[24] = {};
    if (MatterPlatform::pairingCodes(payload, sizeof(payload), manual, sizeof(manual)) != ESP_OK) {
        return sendError(request, 503, "commissioning_unavailable");
    }
    // QR modules rendered here, so the page needs no QR library.
    std::unique_ptr<uint8_t[]> qr(new (std::nothrow) uint8_t[qrcodegen_BUFFER_LEN_FOR_VERSION(6)]);
    std::unique_ptr<uint8_t[]> temp(new (std::nothrow) uint8_t[qrcodegen_BUFFER_LEN_FOR_VERSION(6)]);
    if (!qr || !temp ||
        !qrcodegen_encodeText(payload, temp.get(), qr.get(), qrcodegen_Ecc_MEDIUM, 1, 6,
                              qrcodegen_Mask_AUTO, true)) {
        return sendError(request, 500, "qr_failed");
    }
    const int size = qrcodegen_getSize(qr.get());
    std::string modules;
    modules.reserve(static_cast<size_t>(size * size));
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            modules.push_back(qrcodegen_getModule(qr.get(), x, y) ? '1' : '0');
        }
    }
    cJSON* json = cJSON_CreateObject();
    cJSON_AddBoolToObject(json, "open", MatterPlatform::commissioningWindowOpen());
    cJSON_AddNumberToObject(json, "secondsLeft", self(request)->commissioningSecondsLeft());
    cJSON_AddStringToObject(json, "qrPayload", payload);
    cJSON_AddStringToObject(json, "manualCode", manual);
    cJSON_AddNumberToObject(json, "qrSize", size);
    cJSON_AddStringToObject(json, "qrModules", modules.c_str());
    return sendJson(request, json);
}

esp_err_t sendUpdate(httpd_req_t* request)
{
    const UpdateStatus status = UpdateService::instance().status();
    cJSON* json = cJSON_CreateObject();
    cJSON_AddBoolToObject(json, "enabled", status.enabled);
    cJSON_AddStringToObject(json, "state", toString(status.state));
    cJSON_AddStringToObject(json, "current", status.current);
    cJSON_AddStringToObject(json, "latest", status.latest);
    cJSON_AddStringToObject(json, "variant", status.variant);
    cJSON_AddStringToObject(json, "error", status.error);
    cJSON_AddNumberToObject(json, "progress", status.progress);
    cJSON_AddBoolToObject(json, "autoInstall", status.autoInstall);
    cJSON_AddStringToObject(json, "channel", status.prerelease ? "prerelease" : "release");
    cJSON_AddNumberToObject(json, "checkedAgoS",
                            status.lastCheckUs == 0
                                ? -1
                                : static_cast<double>((esp_timer_get_time() - status.lastCheckUs) / 1000000));
    return sendJson(request, json);
}

esp_err_t handleUpdateGet(httpd_req_t* request)
{
    if (requireAuth(request, false) != ESP_OK) return ESP_OK;
    return sendUpdate(request);
}

esp_err_t handleUpdatePut(httpd_req_t* request)
{
    if (requireAuth(request, true) != ESP_OK) return ESP_OK;
    const std::string body = readBody(request);
    cJSON* root = cJSON_Parse(body.c_str());
    if (root == nullptr) return sendError(request, 400, "invalid_json");
    const cJSON* autoInstall = cJSON_GetObjectItem(root, "autoInstall");
    const cJSON* channel = cJSON_GetObjectItem(root, "channel");
    const bool channelValid = cJSON_IsString(channel) && (std::strcmp(channel->valuestring, "release") == 0 ||
                                                          std::strcmp(channel->valuestring, "prerelease") == 0);
    const bool valid = (cJSON_IsBool(autoInstall) || channelValid) &&
                       (autoInstall == nullptr || cJSON_IsBool(autoInstall)) &&
                       (channel == nullptr || channelValid);
    esp_err_t err = ESP_OK;
    if (valid && cJSON_IsBool(autoInstall)) err = UpdateService::instance().setAutoInstall(cJSON_IsTrue(autoInstall));
    if (valid && err == ESP_OK && channelValid) {
        err = UpdateService::instance().setPrerelease(std::strcmp(channel->valuestring, "prerelease") == 0);
    }
    cJSON_Delete(root);
    if (!valid) return sendError(request, 400, "invalid_update_settings");
    if (err != ESP_OK) return sendError(request, 500, "config_save_failed");
    return sendUpdate(request);
}

esp_err_t handleUpdateCheck(httpd_req_t* request)
{
    if (requireAuth(request, true) != ESP_OK) return ESP_OK;
    const esp_err_t err = UpdateService::instance().checkNow();
    if (err == ESP_ERR_NOT_SUPPORTED) return sendError(request, 501, "updates_disabled");
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return sendError(request, 500, "update_busy");
    return sendUpdate(request);
}

esp_err_t handleUpdateInstall(httpd_req_t* request)
{
    if (requireAuth(request, true) != ESP_OK) return ESP_OK;
    const esp_err_t err = UpdateService::instance().installNow();
    if (err == ESP_ERR_NOT_FOUND) return sendError(request, 409, "no_update_available");
    if (err != ESP_OK) return sendError(request, 409, "update_busy");
    return sendUpdate(request);
}

// Appends `text` as a JSON string (quotes included) to `out`.
void appendJsonString(std::string& out, const char* text)
{
    out.push_back('"');
    for (const char* c = text; *c != '\0'; ++c) {
        const unsigned char ch = static_cast<unsigned char>(*c);
        if (ch == '"' || ch == '\\') {
            out.push_back('\\');
            out.push_back(static_cast<char>(ch));
        } else if (ch < 0x20) {
            char escaped[8];
            std::snprintf(escaped, sizeof(escaped), "\\u%04x", ch);
            out += escaped;
        } else {
            out.push_back(static_cast<char>(ch));
        }
    }
    out.push_back('"');
}

// New log lines after ?since=<seq>, oldest first, at most 48 per call.
esp_err_t handleLogsGet(httpd_req_t* request)
{
    if (requireAuth(request, false) != ESP_OK) return ESP_OK;
    uint32_t since = 0;
    char query[48] = {};
    char value[16] = {};
    if (httpd_req_get_url_query_str(request, query, sizeof(query)) == ESP_OK &&
        httpd_query_key_value(query, "since", value, sizeof(value)) == ESP_OK) {
        since = static_cast<uint32_t>(std::strtoul(value, nullptr, 10));
    }
    constexpr size_t kBatch = 48;
    std::unique_ptr<LogEntry[]> entries(new (std::nothrow) LogEntry[kBatch]);
    if (!entries) return sendError(request, 500, "out_of_memory");
    bool dropped = false;
    const size_t count = LogBuffer::read(since, entries.get(), kBatch, dropped);

    std::string body;
    body.reserve(256 + count * 160);
    // Wall-clock time once SNTP has set it, so the page can show real times.
    char epoch[24] = "null";
    if (ScheduleService::instance().timeValid()) {
        std::snprintf(epoch, sizeof(epoch), "%lld", static_cast<long long>(std::time(nullptr)));
    }
    const uint32_t last = LogBuffer::lastSeq();
    const uint32_t next = count ? entries[count - 1].seq : (since > last ? last : since);
    char head[200];
    std::snprintf(head, sizeof(head),
                  "{\"next\":%lu,\"last\":%lu,\"dropped\":%s,\"nowMs\":%lu,\"epoch\":%s,\"level\":\"%s\",\"lines\":[",
                  static_cast<unsigned long>(next), static_cast<unsigned long>(last),
                  dropped ? "true" : "false", static_cast<unsigned long>(esp_log_timestamp()), epoch,
                  LogBuffer::debug() ? "debug" : "normal");
    body += head;
    for (size_t i = 0; i < count; ++i) {
        char prefix[64];
        std::snprintf(prefix, sizeof(prefix), "%s{\"seq\":%lu,\"ms\":%lu,\"level\":\"%c\",\"tag\":",
                      i ? "," : "", static_cast<unsigned long>(entries[i].seq),
                      static_cast<unsigned long>(entries[i].ms), entries[i].level);
        body += prefix;
        appendJsonString(body, entries[i].tag);
        body += ",\"message\":";
        appendJsonString(body, entries[i].message);
        body.push_back('}');
    }
    body += "]}";
    httpd_resp_set_type(request, "application/json");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    return httpd_resp_send(request, body.data(), static_cast<ssize_t>(body.size()));
}

esp_err_t handleLogsPut(httpd_req_t* request)
{
    if (requireAuth(request, true) != ESP_OK) return ESP_OK;
    const std::string body = readBody(request);
    cJSON* root = cJSON_Parse(body.c_str());
    if (root == nullptr) return sendError(request, 400, "invalid_json");
    const cJSON* level = cJSON_GetObjectItem(root, "level");
    const bool valid = cJSON_IsString(level) && (std::strcmp(level->valuestring, "debug") == 0 ||
                                                 std::strcmp(level->valuestring, "normal") == 0);
    if (valid) LogBuffer::setDebug(std::strcmp(level->valuestring, "debug") == 0);
    cJSON_Delete(root);
    if (!valid) return sendError(request, 400, "invalid_log_level");
    cJSON* json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "level", LogBuffer::debug() ? "debug" : "normal");
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
    // Restore sign-ins that asked to stay signed in; a size mismatch just starts empty.
    if (ConnectorSecretStore::loadSessions(sessions_, sizeof(sessions_)) == ESP_OK) {
        for (Session& session : sessions_) {
            if (!session.remembered) session = Session{};
        }
    } else {
        for (Session& session : sessions_) session = Session{};
    }
    httpd_config_t httpConfig = HTTPD_DEFAULT_CONFIG();
    httpConfig.server_port = CONFIG_OPENCOMFORT_RUNTIME_HTTP_PORT;
    httpConfig.max_uri_handlers = 32;
    httpConfig.stack_size = 8192;
    // Browsers keep idle connections open; recycle the oldest instead of refusing new ones.
    httpConfig.lru_purge_enable = true;
    esp_err_t err = httpd_start(&server_, &httpConfig);
    if (err != ESP_OK) {
        status_ = {"web", ConnectorState::Degraded, err, 0};
        return err;
    }

    err = registerRoute(server_, "/", HTTP_GET, handleRoot, this);
    if (err == ESP_OK) err = registerRoute(server_, "/css/app.css", HTTP_GET, handleCss, this);
    if (err == ESP_OK) err = registerRoute(server_, "/js/runtime.js", HTTP_GET, handleRuntimeJs, this);
    if (err == ESP_OK) err = registerRoute(server_, "/fonts/geist.woff2", HTTP_GET, handleFont, this);
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
    if (err == ESP_OK) {
        err = registerRoute(server_, "/api/v1/firmware", HTTP_POST, handleFirmwarePost, this);
    }
    if (err == ESP_OK) err = registerRoute(server_, "/api/v1/status", HTTP_GET, handleStatus, this);
    if (err == ESP_OK) err = registerRoute(server_, "/api/v1/logs", HTTP_GET, handleLogsGet, this);
    if (err == ESP_OK) err = registerRoute(server_, "/api/v1/logs", HTTP_PUT, handleLogsPut, this);
    if (err == ESP_OK) err = registerRoute(server_, "/api/v1/update", HTTP_GET, handleUpdateGet, this);
    if (err == ESP_OK) err = registerRoute(server_, "/api/v1/update", HTTP_PUT, handleUpdatePut, this);
    if (err == ESP_OK) {
        err = registerRoute(server_, "/api/v1/update/check", HTTP_POST, handleUpdateCheck, this);
    }
    if (err == ESP_OK) {
        err = registerRoute(server_, "/api/v1/update/install", HTTP_POST, handleUpdateInstall, this);
    }
    if (err == ESP_OK) err = registerRoute(server_, "/api/v1/history", HTTP_GET, handleHistory, this);
    if (err == ESP_OK) err = registerRoute(server_, "/api/v1/schedule", HTTP_GET, handleScheduleGet, this);
    if (err == ESP_OK) err = registerRoute(server_, "/api/v1/schedule", HTTP_PUT, handleSchedulePut, this);
    if (err == ESP_OK) {
        err = registerRoute(server_, "/api/v1/schedule/sleep", HTTP_POST, handleSleepPost, this);
    }
    if (err == ESP_OK) {
        err = registerRoute(server_, "/api/v1/matter/pairing", HTTP_GET, handleMatterPairing, this);
    }
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

void RuntimeHttpConnector::setConnectorManager(const ConnectorManager* manager)
{
    connectorManager_ = manager;
}

const ConnectorManager* RuntimeHttpConnector::connectorManager() const
{
    return connectorManager_;
}

size_t RuntimeHttpConnector::copyHistory(int16_t* out, size_t capacity) const
{
    const size_t count = historyCount_ < capacity ? historyCount_ : capacity;
    const size_t start = (historyHead_ + kHistoryLength - historyCount_) % kHistoryLength;
    for (size_t i = 0; i < count; ++i) {
        out[i] = history_[(start + i) % kHistoryLength];
    }
    return count;
}

void RuntimeHttpConnector::markCommissioningOpened()
{
    commissioningOpenedAt_ = xTaskGetTickCount();
}

uint32_t RuntimeHttpConnector::commissioningSecondsLeft() const
{
    constexpr uint32_t kWindowS = 300;  // MatterPlatformEsp kCommissioningWindow
    if (commissioningOpenedAt_ == 0 || !MatterPlatform::commissioningWindowOpen()) return 0;
    const uint32_t elapsed = (xTaskGetTickCount() - commissioningOpenedAt_) / configTICK_RATE_HZ;
    return elapsed < kWindowS ? kWindowS - elapsed : 0;
}

void RuntimeHttpConnector::tick()
{
    const TickType_t now = xTaskGetTickCount();
    if (!mdnsStarted_ && mdnsDeferredSince_ != 0 && static_cast<int32_t>(now - mdnsRetryAt_) >= 0) {
        mdnsRetryAt_ = now + pdMS_TO_TICKS(2000);
        if (!startSharedMdns() && now - mdnsDeferredSince_ > pdMS_TO_TICKS(30000) &&
            !MatterPlatform::isRunning()) {
            // Matter did not come up: run mDNS ourselves so the portal stays reachable.
            mdnsDeferredSince_ = 0;
            const ConnectorConfig saved = config_.connectors;
            config_.connectors.matterEnabled = false;
            (void)startMdns();
            config_.connectors = saved;
        }
    }
    if (static_cast<int32_t>(now - nextSampleAt_) >= 0) {
        nextSampleAt_ = now + pdMS_TO_TICKS(60000);
        const RuntimeSnapshot snapshot = runtime_.snapshot();
        history_[historyHead_] = snapshot.reported.valid
                                     ? static_cast<int16_t>(snapshot.reported.returnAirC * 10.0f)
                                     : INT16_MIN;
        historyHead_ = (historyHead_ + 1) % kHistoryLength;
        if (historyCount_ < kHistoryLength) historyCount_++;
    }
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

namespace {
void randomHex(char* out, size_t bytes)
{
    uint8_t random[16];
    esp_fill_random(random, bytes);
    for (size_t i = 0; i < bytes; ++i) {
        std::snprintf(out + (i * 2), 3, "%02x", random[i]);
    }
}

bool constantTimeEquals(const char* a, const char* b, size_t length)
{
    uint8_t difference = 0;
    for (size_t i = 0; i < length; ++i) {
        difference |= static_cast<uint8_t>(a[i] ^ b[i]);
    }
    return difference == 0;
}

bool expired(const RuntimeHttpConnector::Session& session, TickType_t now)
{
    return session.token[0] == '\0' ||
           (!session.remembered && static_cast<int32_t>(now - session.expiresAt) >= 0);
}
}  // namespace

const RuntimeHttpConnector::Session* RuntimeHttpConnector::authenticate(const char* password, bool remember)
{
    const TickType_t now = xTaskGetTickCount();
    if (static_cast<int32_t>(now - nextLoginAllowedAt_) < 0) {
        return nullptr;
    }
    if (!ConnectorSecretStore::verifyAdminPassword(password)) {
        failedLoginAttempts_++;
        nextLoginAllowedAt_ = now + (failedLoginAttempts_ >= 5 ? kLoginLockout : kLoginDelay);
        return nullptr;
    }

    failedLoginAttempts_ = 0;
    nextLoginAllowedAt_ = 0;
    // Reuse a free or expired slot, otherwise replace the short session that expires
    // first; only when every slot is remembered does the first remembered one go.
    Session* slot = nullptr;
    for (Session& candidate : sessions_) {
        if (expired(candidate, now)) {
            slot = &candidate;
            break;
        }
        if (!candidate.remembered &&
            (slot == nullptr || static_cast<int32_t>(candidate.expiresAt - slot->expiresAt) < 0)) {
            slot = &candidate;
        }
    }
    if (slot == nullptr) slot = &sessions_[0];
    const bool replacedRemembered = slot->remembered;
    randomHex(slot->token, 16);
    randomHex(slot->csrf, 16);
    slot->expiresAt = now + kSessionDuration;
    slot->remembered = remember;
    if (remember || replacedRemembered) saveRememberedSessions();
    return slot;
}

const char* RuntimeHttpConnector::sessionCookieValue(const char* cookieHeader)
{
    // "oc_session=" at the start or after "; ", followed by exactly 32 characters.
    constexpr const char* kName = "oc_session=";
    const size_t nameLength = std::strlen(kName);
    for (const char* at = std::strstr(cookieHeader, kName); at != nullptr;
         at = std::strstr(at + 1, kName)) {
        if (at != cookieHeader && !(at >= cookieHeader + 2 && at[-2] == ';' && at[-1] == ' ')) {
            continue;
        }
        const char* value = at + nameLength;
        const size_t length = std::strcspn(value, ";");
        return length == 32 ? value : nullptr;
    }
    return nullptr;
}

RuntimeHttpConnector::Session* RuntimeHttpConnector::findSession(httpd_req_t* request) const
{
    char cookie[256] = {};
    if (httpd_req_get_hdr_value_str(request, "Cookie", cookie, sizeof(cookie)) != ESP_OK) {
        return nullptr;
    }
    const char* value = sessionCookieValue(cookie);
    if (value == nullptr) {
        return nullptr;
    }
    const TickType_t now = xTaskGetTickCount();
    for (Session& session : sessions_) {
        if (!expired(session, now) && constantTimeEquals(session.token, value, 32)) {
            return &session;
        }
    }
    return nullptr;
}

bool RuntimeHttpConnector::isAuthorized(httpd_req_t* request, bool requireCsrf) const
{
    const Session* session = findSession(request);
    if (session == nullptr) {
        return false;
    }
    if (!requireCsrf) {
        return true;
    }
    char csrf[40] = {};
    return httpd_req_get_hdr_value_str(request, "X-OC-CSRF", csrf, sizeof(csrf)) == ESP_OK &&
           std::strlen(csrf) == 32 && constantTimeEquals(csrf, session->csrf, 32);
}

void RuntimeHttpConnector::logout(httpd_req_t* request)
{
    if (Session* session = findSession(request)) {
        const bool remembered = session->remembered;
        *session = Session{};
        if (remembered) saveRememberedSessions();
    }
}

void RuntimeHttpConnector::saveRememberedSessions() const
{
    Session saved[kMaxSessions] = {};
    for (size_t i = 0; i < kMaxSessions; ++i) {
        if (sessions_[i].remembered) saved[i] = sessions_[i];
    }
    const esp_err_t err = ConnectorSecretStore::saveSessions(saved, sizeof(saved));
    if (err != ESP_OK) ESP_LOGW(kTag, "Saving sign-ins failed: %s", esp_err_to_name(err));
}

void RuntimeHttpConnector::scheduleRestart()
{
    restartAt_ = xTaskGetTickCount() + pdMS_TO_TICKS(1000);
}

bool RuntimeHttpConnector::startSharedMdns()
{
    esp_netif_ip_info_t ip{};
    esp_netif_t* sta = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (sta == nullptr || esp_netif_get_ip_info(sta, &ip) != ESP_OK || ip.ip.addr == 0) {
        return false;
    }
    if (resolveDeviceHostname(config_, mdnsHostname_, sizeof(mdnsHostname_)) != ESP_OK) return false;
    const char* hostname = mdnsHostname_;
    copyBounded(mdnsInstance_, sizeof(mdnsInstance_), config_.friendlyName);
    // shortcut: the delegate keeps the address from registration; a DHCP address change
    // needs a restart to be picked up.
    mdns_ip_addr_t address{};
    address.addr.type = ESP_IPADDR_TYPE_V4;
    address.addr.u_addr.ip4.addr = ip.ip.addr;
    address.next = nullptr;
    if (mdns_delegate_hostname_add(hostname, &address) != ESP_OK) {
        return false;  // ESP-Matter has not started mDNS yet
    }
    if (mdns_service_add_for_host(mdnsInstance_, "_http", "_tcp", hostname,
                                  CONFIG_OPENCOMFORT_RUNTIME_HTTP_PORT, nullptr, 0) != ESP_OK) {
        mdns_delegate_hostname_remove(hostname);
        return false;
    }
    mdnsShared_ = true;
    mdnsStarted_ = true;
    ESP_LOGI(kTag, "mDNS: %s.local shared with Matter", hostname);
    return true;
}

bool RuntimeHttpConnector::startMdns()
{
    if (mdnsStarted_) return true;
    if (product_.supportsMatter && config_.connectors.matterEnabled) {
        // ESP-Matter calls mdns_init() itself and fails if it already ran; tick() registers later.
        if (mdnsDeferredSince_ == 0) mdnsDeferredSince_ = xTaskGetTickCount();
        return false;
    }
    if (resolveDeviceHostname(config_, mdnsHostname_, sizeof(mdnsHostname_)) != ESP_OK) return false;
    if (mdns_init() != ESP_OK) return false;
    const char* hostname = mdnsHostname_;
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
    if (mdnsStarted_ && mdnsShared_) {
        mdns_service_remove_for_host(mdnsInstance_, "_http", "_tcp", mdnsHostname_);
        mdns_delegate_hostname_remove(mdnsHostname_);
    } else if (mdnsStarted_) {
        mdns_free();
    }
    mdnsStarted_ = false;
    mdnsShared_ = false;
    mdnsDeferredSince_ = 0;
}

}  // namespace opencomfort
