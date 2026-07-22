#include "src/Http/CaptivePortal.hpp"

#include <cstdio>
#include <cstring>
#include <string>

#include "src/Config/UserConfigStore.hpp"
#include "src/Setup/AcSetupProvider.hpp"
#include "src/Setup/ISetupProvider.hpp"
#include "src/Wifi/WiFiService.hpp"

extern "C" {
#include "cJSON.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
}

namespace opencomfort {
namespace {

constexpr const char* kTag = "CaptivePortal";
constexpr uint16_t kDnsPort = 53;
constexpr size_t kMaxBody = 2048;

struct PortalContext {
    const ProductDefinition* product = nullptr;
    ISetupProvider* setup = nullptr;
    SetupDraft draft = {};
    bool wifiConfigured = false;
    bool moduleConfigured = false;
    bool finished = false;
    bool success = false;
    UserConfig result = {};
    httpd_handle_t server = nullptr;
    TaskHandle_t dnsTask = nullptr;
    volatile bool dnsRunning = false;
};

PortalContext* g_ctx = nullptr;

std::string readBody(httpd_req_t* req)
{
    if (req->content_len <= 0 || req->content_len > static_cast<ssize_t>(kMaxBody)) {
        return {};
    }
    std::string body(static_cast<size_t>(req->content_len), '\0');
    int received = 0;
    while (received < req->content_len) {
        const int ret = httpd_req_recv(req, body.data() + received, req->content_len - received);
        if (ret <= 0) {
            return {};
        }
        received += ret;
    }
    return body;
}

esp_err_t sendJson(httpd_req_t* req, cJSON* json, int status = 200)
{
    char* printed = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);
    if (printed == nullptr) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        return httpd_resp_send(req, "{\"error\":\"oom\"}", HTTPD_RESP_USE_STRLEN);
    }

    char statusText[32];
    std::snprintf(statusText, sizeof(statusText), "%d", status);
    httpd_resp_set_status(req, statusText);
    httpd_resp_set_type(req, "application/json");
    const esp_err_t err = httpd_resp_send(req, printed, HTTPD_RESP_USE_STRLEN);
    cJSON_free(printed);
    return err;
}

esp_err_t sendError(httpd_req_t* req, int status, const char* message)
{
    cJSON* json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "error", message);
    return sendJson(req, json, status);
}

esp_err_t sendFile(httpd_req_t* req, const char* path, const char* type)
{
    FILE* file = std::fopen(path, "r");
    if (file == nullptr) {
        return sendError(req, 404, "not_found");
    }

    httpd_resp_set_type(req, type);
    char buffer[512];
    while (true) {
        const size_t n = std::fread(buffer, 1, sizeof(buffer), file);
        if (n == 0) {
            break;
        }
        if (httpd_resp_send_chunk(req, buffer, n) != ESP_OK) {
            std::fclose(file);
            httpd_resp_sendstr_chunk(req, nullptr);
            return ESP_FAIL;
        }
    }
    std::fclose(file);
    return httpd_resp_send_chunk(req, nullptr, 0);
}

void dnsTask(void* /*arg*/)
{
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock < 0) {
        vTaskDelete(nullptr);
        return;
    }

    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(kDnsPort);
    bind(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));

    uint8_t buffer[512];
    while (g_ctx != nullptr && g_ctx->dnsRunning) {
        sockaddr_in source = {};
        socklen_t sourceLen = sizeof(source);
        const int len =
            recvfrom(sock, buffer, sizeof(buffer), 0, reinterpret_cast<sockaddr*>(&source), &sourceLen);
        if (len < 12) {
            continue;
        }

        // Minimal DNS response: point every A query to SoftAP IP.
        buffer[2] = 0x81;
        buffer[3] = 0x80;
        buffer[6] = 0x00;
        buffer[7] = 0x01;
        buffer[8] = 0x00;
        buffer[9] = 0x00;
        buffer[10] = 0x00;
        buffer[11] = 0x00;

        size_t responseLen = static_cast<size_t>(len);
        if (responseLen + 16 < sizeof(buffer)) {
            buffer[responseLen++] = 0xC0;
            buffer[responseLen++] = 0x0C;
            buffer[responseLen++] = 0x00;
            buffer[responseLen++] = 0x01;
            buffer[responseLen++] = 0x00;
            buffer[responseLen++] = 0x01;
            buffer[responseLen++] = 0x00;
            buffer[responseLen++] = 0x00;
            buffer[responseLen++] = 0x00;
            buffer[responseLen++] = 0x3C;
            buffer[responseLen++] = 0x00;
            buffer[responseLen++] = 0x04;
            buffer[responseLen++] = 192;
            buffer[responseLen++] = 168;
            buffer[responseLen++] = 4;
            buffer[responseLen++] = 1;
            sendto(sock, buffer, responseLen, 0, reinterpret_cast<sockaddr*>(&source), sourceLen);
        }
    }

    close(sock);
    vTaskDelete(nullptr);
}

esp_err_t handleRoot(httpd_req_t* req)
{
    return sendFile(req, "/spiffs/www/index.html", "text/html");
}

esp_err_t handleCss(httpd_req_t* req)
{
    return sendFile(req, "/spiffs/www/css/app.css", "text/css");
}

esp_err_t handleJs(httpd_req_t* req)
{
    return sendFile(req, "/spiffs/www/js/app.js", "application/javascript");
}

esp_err_t handleSession(httpd_req_t* req)
{
    cJSON* json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "productId", g_ctx->product->productId);
    cJSON_AddStringToObject(json, "module", toString(g_ctx->product->module));
    cJSON_AddBoolToObject(json, "wifiConfigured", g_ctx->wifiConfigured);
    cJSON_AddBoolToObject(json, "moduleConfigured", g_ctx->moduleConfigured);
    return sendJson(req, json);
}

esp_err_t handleWifiScan(httpd_req_t* req)
{
    WifiScanEntry entries[24];
    size_t count = 0;
    if (WiFiService::scan(entries, 24, count, 5000) != ESP_OK) {
        return sendError(req, 500, "scan_failed");
    }

    cJSON* json = cJSON_CreateObject();
    cJSON* networks = cJSON_AddArrayToObject(json, "networks");
    for (size_t i = 0; i < count; ++i) {
        cJSON* item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "ssid", entries[i].ssid);
        cJSON_AddNumberToObject(item, "rssi", entries[i].rssi);
        cJSON_AddNumberToObject(item, "authmode", entries[i].authmode);
        cJSON_AddItemToArray(networks, item);
    }
    return sendJson(req, json);
}

esp_err_t handleWifiPut(httpd_req_t* req)
{
    const std::string body = readBody(req);
    cJSON* root = cJSON_Parse(body.c_str());
    if (root == nullptr) {
        return sendError(req, 400, "invalid_json");
    }

    const cJSON* ssid = cJSON_GetObjectItem(root, "ssid");
    const cJSON* password = cJSON_GetObjectItem(root, "password");
    if (!cJSON_IsString(ssid) || ssid->valuestring == nullptr || ssid->valuestring[0] == '\0') {
        cJSON_Delete(root);
        return sendError(req, 400, "ssid_required");
    }

    WifiCredentials credentials;
    copyBounded(credentials.ssid, sizeof(credentials.ssid), ssid->valuestring);
    if (cJSON_IsString(password) && password->valuestring != nullptr) {
        copyBounded(credentials.password, sizeof(credentials.password), password->valuestring);
    }
    cJSON_Delete(root);

    if (WiFiService::connectStation(credentials, 20000) != ESP_OK) {
        return sendError(req, 400, "wifi_connect_failed");
    }

    g_ctx->draft.wifi = credentials;
    g_ctx->draft.wifiVerified = true;
    g_ctx->wifiConfigured = true;

    cJSON* json = cJSON_CreateObject();
    cJSON_AddBoolToObject(json, "ok", true);
    return sendJson(req, json);
}

esp_err_t handleSetupSchema(httpd_req_t* req)
{
    SetupSchema schema;
    if (!g_ctx->setup->buildSchema(schema)) {
        return sendError(req, 500, "schema_unavailable");
    }

    cJSON* json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "moduleId", schema.moduleId);
    cJSON* steps = cJSON_AddArrayToObject(json, "steps");
    for (size_t i = 0; i < schema.stepCount; ++i) {
        const SetupStep& step = schema.steps[i];
        cJSON* stepJson = cJSON_CreateObject();
        cJSON_AddStringToObject(stepJson, "id", step.id);
        cJSON_AddStringToObject(stepJson, "title", step.title);
        cJSON_AddStringToObject(stepJson, "description", step.description);
        cJSON* fields = cJSON_AddArrayToObject(stepJson, "fields");
        for (size_t f = 0; f < step.fieldCount; ++f) {
            const SetupField& field = step.fields[f];
            cJSON* fieldJson = cJSON_CreateObject();
            cJSON_AddStringToObject(fieldJson, "id", field.id);
            cJSON_AddStringToObject(fieldJson, "label", field.label);
            cJSON_AddBoolToObject(fieldJson, "required", field.required);
            const char* type = "text";
            if (field.type == SetupFieldType::Password) {
                type = "password";
            } else if (field.type == SetupFieldType::Select) {
                type = "select";
            } else if (field.type == SetupFieldType::Number) {
                type = "number";
            }
            cJSON_AddStringToObject(fieldJson, "type", type);
            cJSON* options = cJSON_AddArrayToObject(fieldJson, "options");
            for (size_t o = 0; o < field.optionCount; ++o) {
                cJSON* option = cJSON_CreateObject();
                cJSON_AddStringToObject(option, "value", field.options[o].value);
                cJSON_AddStringToObject(option, "label", field.options[o].label);
                cJSON_AddItemToArray(options, option);
            }
            cJSON_AddItemToArray(fields, fieldJson);
        }
        cJSON_AddItemToArray(steps, stepJson);
    }
    return sendJson(req, json);
}

esp_err_t handleSetupPut(httpd_req_t* req)
{
    if (!g_ctx->wifiConfigured) {
        return sendError(req, 400, "wifi_required");
    }

    const std::string body = readBody(req);
    cJSON* root = cJSON_Parse(body.c_str());
    if (root == nullptr) {
        return sendError(req, 400, "invalid_json");
    }

    const cJSON* model = cJSON_GetObjectItem(root, "model");
    const cJSON* friendly = cJSON_GetObjectItem(root, "friendlyName");
    if (!cJSON_IsString(model) || model->valuestring == nullptr) {
        cJSON_Delete(root);
        return sendError(req, 400, "model_required");
    }

    const char* modelValue = model->valuestring;
    const char* separator = std::strchr(modelValue, '|');
    if (separator == nullptr) {
        cJSON_Delete(root);
        return sendError(req, 400, "invalid_model");
    }

    char profileId[49] = {};
    char modelId[49] = {};
    const size_t profileLen = static_cast<size_t>(separator - modelValue);
    if (profileLen >= sizeof(profileId)) {
        cJSON_Delete(root);
        return sendError(req, 400, "invalid_model");
    }
    std::memcpy(profileId, modelValue, profileLen);
    profileId[profileLen] = '\0';
    copyBounded(modelId, sizeof(modelId), separator + 1);

    copyBounded(g_ctx->draft.profileId, sizeof(g_ctx->draft.profileId), profileId);
    copyBounded(g_ctx->draft.modelId, sizeof(g_ctx->draft.modelId), modelId);
    if (cJSON_IsString(friendly) && friendly->valuestring != nullptr) {
        copyBounded(g_ctx->draft.friendlyName, sizeof(g_ctx->draft.friendlyName),
                    friendly->valuestring);
    }
    cJSON_Delete(root);

    char error[96] = {};
    if (!g_ctx->setup->validateDraft(g_ctx->draft, error, sizeof(error))) {
        return sendError(req, 400, error);
    }

    g_ctx->moduleConfigured = true;
    cJSON* json = cJSON_CreateObject();
    cJSON_AddBoolToObject(json, "ok", true);
    return sendJson(req, json);
}

esp_err_t handleCommit(httpd_req_t* req)
{
    if (!g_ctx->wifiConfigured || !g_ctx->moduleConfigured) {
        return sendError(req, 400, "setup_incomplete");
    }

    char error[96] = {};
    UserConfig config;
    if (!g_ctx->setup->finalize(g_ctx->draft, config, error, sizeof(error))) {
        return sendError(req, 400, error);
    }

    if (WiFiService::saveCredentials(g_ctx->draft.wifi) != ESP_OK) {
        return sendError(req, 500, "wifi_save_failed");
    }

    if (UserConfigStore::save(config) != ConfigStatus::Ok) {
        return sendError(req, 500, "config_save_failed");
    }

    g_ctx->result = config;
    g_ctx->success = true;
    g_ctx->finished = true;

    cJSON* json = cJSON_CreateObject();
    cJSON_AddBoolToObject(json, "ok", true);
    cJSON_AddStringToObject(json, "message", "Provisioning complete. Device will reboot.");
    sendJson(req, json);
    return ESP_OK;
}

esp_err_t handleCaptiveProbe(httpd_req_t* req)
{
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
    return httpd_resp_send(req, nullptr, 0);
}

esp_err_t startHttpServer(PortalContext& ctx)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.max_uri_handlers = 16;
    config.stack_size = 8192;

    if (httpd_start(&ctx.server, &config) != ESP_OK) {
        return ESP_FAIL;
    }

    const httpd_uri_t routeRoot = {"/", HTTP_GET, handleRoot, nullptr};
    const httpd_uri_t routeCss = {"/css/app.css", HTTP_GET, handleCss, nullptr};
    const httpd_uri_t routeJs = {"/js/app.js", HTTP_GET, handleJs, nullptr};
    const httpd_uri_t routeSession = {"/api/v1/session", HTTP_GET, handleSession, nullptr};
    const httpd_uri_t routeScan = {"/api/v1/wifi/networks", HTTP_GET, handleWifiScan, nullptr};
    const httpd_uri_t routeWifi = {"/api/v1/wifi", HTTP_PUT, handleWifiPut, nullptr};
    const httpd_uri_t routeSchema = {"/api/v1/setup/schema", HTTP_GET, handleSetupSchema, nullptr};
    const httpd_uri_t routeModule = {"/api/v1/setup/module", HTTP_PUT, handleSetupPut, nullptr};
    const httpd_uri_t routeCommit = {"/api/v1/setup/commit", HTTP_POST, handleCommit, nullptr};
    const httpd_uri_t route204 = {"/generate_204", HTTP_GET, handleCaptiveProbe, nullptr};
    const httpd_uri_t routeApple = {"/hotspot-detect.html", HTTP_GET, handleCaptiveProbe, nullptr};
    const httpd_uri_t routeMs = {"/connecttest.txt", HTTP_GET, handleCaptiveProbe, nullptr};

    httpd_register_uri_handler(ctx.server, &routeRoot);
    httpd_register_uri_handler(ctx.server, &routeCss);
    httpd_register_uri_handler(ctx.server, &routeJs);
    httpd_register_uri_handler(ctx.server, &routeSession);
    httpd_register_uri_handler(ctx.server, &routeScan);
    httpd_register_uri_handler(ctx.server, &routeWifi);
    httpd_register_uri_handler(ctx.server, &routeSchema);
    httpd_register_uri_handler(ctx.server, &routeModule);
    httpd_register_uri_handler(ctx.server, &routeCommit);
    httpd_register_uri_handler(ctx.server, &route204);
    httpd_register_uri_handler(ctx.server, &routeApple);
    httpd_register_uri_handler(ctx.server, &routeMs);
    return ESP_OK;
}

}  // namespace

CaptivePortal::CaptivePortal(const ProductDefinition& product) : product_(product) {}

CaptivePortal::~CaptivePortal() = default;

bool CaptivePortal::run(UserConfig& outConfig)
{
    PortalContext ctx;
    ctx.product = &product_;
    ctx.setup = &currentSetupProvider();
    g_ctx = &ctx;

    if (WiFiService::init() != ESP_OK) {
        ESP_LOGE(kTag, "WiFi init failed");
        g_ctx = nullptr;
        return false;
    }

    uint8_t mac[6] = {};
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    char ssid[32] = {};
    std::snprintf(ssid, sizeof(ssid), "OpenComfort-%02X%02X", mac[4], mac[5]);

    if (WiFiService::startSoftAp(ssid, product_.portalApPassword) != ESP_OK) {
        ESP_LOGE(kTag, "SoftAP start failed");
        g_ctx = nullptr;
        return false;
    }

    ctx.dnsRunning = true;
    xTaskCreate(dnsTask, "dns_hijack", 3072, nullptr, 3, &ctx.dnsTask);

    if (startHttpServer(ctx) != ESP_OK) {
        ESP_LOGE(kTag, "HTTP server start failed");
        ctx.dnsRunning = false;
        g_ctx = nullptr;
        return false;
    }

    ESP_LOGI(kTag, "Captive portal ready on SoftAP %s", ssid);
    while (!ctx.finished) {
        vTaskDelay(pdMS_TO_TICKS(200));
    }

    if (ctx.server != nullptr) {
        httpd_stop(ctx.server);
        ctx.server = nullptr;
    }
    ctx.dnsRunning = false;
    vTaskDelay(pdMS_TO_TICKS(100));
    WiFiService::stopSoftAp();

    outConfig = ctx.result;
    const bool ok = ctx.success;
    g_ctx = nullptr;
    return ok;
}

}  // namespace opencomfort
