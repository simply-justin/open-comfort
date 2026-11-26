#include "src/Http/CaptivePortal.hpp"
#include "src/Http/WebAssets.hpp"

#include <cstdio>
#include <cstring>
#include <memory>
#include <new>
#include <string>

#include "src/Config/ConnectorSecretStore.hpp"
#include "src/Config/UserConfigStore.hpp"
#include "src/Profiles/ProfileRepository.hpp"
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
    bool connectorsConfigured = false;
    bool finished = false;
    bool success = false;
    UserConfig result = {};
    httpd_handle_t server = nullptr;
    volatile bool dnsAlive = false;  // set before the task starts, cleared as it exits
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

// Offset just past the single question, or 0 when the query is not one we answer.
size_t questionEnd(const uint8_t* packet, size_t length)
{
    if (packet[4] != 0 || packet[5] != 1) return 0;  // exactly one question
    size_t at = 12;
    while (at < length && packet[at] != 0) {
        if ((packet[at] & 0xC0) != 0) return 0;  // no compression in a question
        at += static_cast<size_t>(packet[at]) + 1;
    }
    at += 1 + 4;  // root label, QTYPE, QCLASS
    return at <= length ? at : 0;
}

// Answers every A query with the SoftAP address, so phones open the setup page.
void dnsTask(void* arg)
{
    auto* ctx = static_cast<PortalContext*>(arg);
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock >= 0) {
        sockaddr_in addr = {};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
        addr.sin_port = htons(kDnsPort);
        // A receive timeout lets the loop notice dnsRunning going false.
        timeval timeout = {0, 500000};
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        if (bind(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
            ESP_LOGW(kTag, "DNS port busy; open http://192.168.4.1 by hand");
            ctx->dnsRunning = false;
        }
    }
    uint8_t buffer[512];
    while (sock >= 0 && ctx->dnsRunning) {
        sockaddr_in source = {};
        socklen_t sourceLen = sizeof(source);
        const int len =
            recvfrom(sock, buffer, sizeof(buffer), 0, reinterpret_cast<sockaddr*>(&source), &sourceLen);
        if (len < 12 || (buffer[2] & 0x80) != 0) {  // too short, or a response
            continue;
        }
        // Drop anything after the question (EDNS records), then answer it.
        const size_t end = questionEnd(buffer, static_cast<size_t>(len));
        if (end == 0 || end + 16 > sizeof(buffer)) {
            continue;
        }
        const bool typeA = buffer[end - 4] == 0x00 && buffer[end - 3] == 0x01;
        buffer[2] = 0x81;
        buffer[3] = 0x80;
        buffer[6] = 0x00;
        buffer[7] = typeA ? 0x01 : 0x00;  // other types (AAAA) get an empty answer
        buffer[8] = 0x00;
        buffer[9] = 0x00;
        buffer[10] = 0x00;
        buffer[11] = 0x00;
        size_t responseLen = end;
        if (typeA) {
            const uint8_t answer[] = {0xC0, 0x0C, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00,
                                      0x00, 0x3C, 0x00, 0x04, 192,  168,  4,    1};
            std::memcpy(buffer + responseLen, answer, sizeof(answer));
            responseLen += sizeof(answer);
        }
        sendto(sock, buffer, responseLen, 0, reinterpret_cast<sockaddr*>(&source), sourceLen);
    }
    if (sock >= 0) close(sock);
    ctx->dnsAlive = false;  // run() waits for this before ctx goes away
    vTaskDelete(nullptr);
}

void stopDns(PortalContext& ctx)
{
    ctx.dnsRunning = false;
    for (int i = 0; i < 20 && ctx.dnsAlive; ++i) vTaskDelay(pdMS_TO_TICKS(100));
}

esp_err_t handleRoot(httpd_req_t* req)
{
    return sendWebAsset(req, WebAsset::SetupHtml);
}

esp_err_t handleCss(httpd_req_t* req)
{
    return sendWebAsset(req, WebAsset::AppCss);
}

esp_err_t handleFont(httpd_req_t* req)
{
    return sendWebAsset(req, WebAsset::Font);
}

esp_err_t handleJs(httpd_req_t* req)
{
    return sendWebAsset(req, WebAsset::SetupJs);
}

esp_err_t handleSession(httpd_req_t* req)
{
    cJSON* json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "productId", g_ctx->product->productId);
    cJSON_AddStringToObject(json, "module", toString(g_ctx->product->module));
    cJSON_AddBoolToObject(json, "wifiConfigured", g_ctx->wifiConfigured);
    cJSON_AddBoolToObject(json, "moduleConfigured", g_ctx->moduleConfigured);
    cJSON_AddBoolToObject(json, "connectorsConfigured", g_ctx->connectorsConfigured);
    cJSON_AddBoolToObject(json, "matterSupported", g_ctx->product->supportsMatter);
    cJSON_AddBoolToObject(json, "mqttSupported", g_ctx->product->supportsMqtt);
    // Same derivation as RuntimeHttpConnector::startMdns, so setup can show the runtime address.
    uint8_t mac[6] = {};
    if (esp_read_mac(mac, ESP_MAC_WIFI_STA) == ESP_OK) {
        char hostname[32];
        std::snprintf(hostname, sizeof(hostname), "opencomfort-%02x%02x.local", mac[4], mac[5]);
        cJSON_AddStringToObject(json, "hostname", hostname);
    }
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
    // Profiles come from the online catalog; the next step lists them.
    (void)ProfileRepository::refreshRemoteCatalog(*g_ctx->product);

    cJSON* json = cJSON_CreateObject();
    cJSON_AddBoolToObject(json, "ok", true);
    return sendJson(req, json);
}

esp_err_t handleSetupSchema(httpd_req_t* req)
{
    // ~30 KB, so on the heap rather than the HTTP task stack.
    std::unique_ptr<SetupSchema> schemaStorage(new (std::nothrow) SetupSchema());
    if (!schemaStorage) {
        return sendError(req, 500, "schema_unavailable");
    }
    if (!g_ctx->setup->buildSchema(*schemaStorage)) {
        // No catalog yet: fetch it once Wi-Fi is up, else the page asks to join first.
        if (!WiFiService::isStationConnected() ||
            ProfileRepository::refreshRemoteCatalog(*g_ctx->product) != ESP_OK ||
            !g_ctx->setup->buildSchema(*schemaStorage)) {
            return sendError(req, 503, "profiles_unavailable");
        }
    }
    const SetupSchema& schema = *schemaStorage;

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

esp_err_t handleConnectorsPut(httpd_req_t* req)
{
    const std::string body = readBody(req);
    cJSON* root = cJSON_Parse(body.c_str());
    if (root == nullptr) {
        return sendError(req, 400, "invalid_json");
    }

    const cJSON* adminPassword = cJSON_GetObjectItem(root, "adminPassword");
    const cJSON* mqttEnabled = cJSON_GetObjectItem(root, "mqttEnabled");
    const cJSON* matterEnabled = cJSON_GetObjectItem(root, "matterEnabled");
    const cJSON* mqttHost = cJSON_GetObjectItem(root, "mqttHost");
    const cJSON* mqttPort = cJSON_GetObjectItem(root, "mqttPort");
    const cJSON* mqttTls = cJSON_GetObjectItem(root, "mqttTls");
    const cJSON* mqttUsername = cJSON_GetObjectItem(root, "mqttUsername");
    const cJSON* mqttPassword = cJSON_GetObjectItem(root, "mqttPassword");

    if (!cJSON_IsString(adminPassword) || adminPassword->valuestring == nullptr ||
        std::strlen(adminPassword->valuestring) < 8) {
        cJSON_Delete(root);
        return sendError(req, 400, "admin_password_too_short");
    }

    const bool enableMqtt = cJSON_IsTrue(mqttEnabled);
    const bool enableMatter = cJSON_IsTrue(matterEnabled);
    if (enableMqtt && !g_ctx->product->supportsMqtt) {
        cJSON_Delete(root);
        return sendError(req, 400, "mqtt_not_supported");
    }
    if (enableMatter && !g_ctx->product->supportsMatter) {
        cJSON_Delete(root);
        return sendError(req, 400, "matter_not_supported");
    }
    // One smart-home integration at a time: both would show the unit twice in Home Assistant.
    if (enableMqtt && enableMatter) {
        cJSON_Delete(root);
        return sendError(req, 400, "choose_one_integration");
    }
    if (enableMqtt &&
        (!cJSON_IsString(mqttHost) || mqttHost->valuestring == nullptr ||
         mqttHost->valuestring[0] == '\0')) {
        cJSON_Delete(root);
        return sendError(req, 400, "mqtt_host_required");
    }

    g_ctx->draft.connectors = ConnectorConfig{};
    g_ctx->draft.connectors.mqttEnabled = enableMqtt;
    g_ctx->draft.connectors.matterEnabled = enableMatter;
    g_ctx->draft.connectors.mqttTls = !cJSON_IsFalse(mqttTls);
    if (cJSON_IsNumber(mqttPort) && mqttPort->valueint > 0 && mqttPort->valueint <= 65535) {
        g_ctx->draft.connectors.mqttPort = static_cast<uint16_t>(mqttPort->valueint);
    }
    if (cJSON_IsString(mqttHost) && mqttHost->valuestring != nullptr) {
        copyBounded(g_ctx->draft.connectors.mqttHost,
                    sizeof(g_ctx->draft.connectors.mqttHost), mqttHost->valuestring);
    }
    if (cJSON_IsString(mqttUsername) && mqttUsername->valuestring != nullptr) {
        copyBounded(g_ctx->draft.mqttCredentials.username,
                    sizeof(g_ctx->draft.mqttCredentials.username), mqttUsername->valuestring);
    }
    if (cJSON_IsString(mqttPassword) && mqttPassword->valuestring != nullptr) {
        copyBounded(g_ctx->draft.mqttCredentials.password,
                    sizeof(g_ctx->draft.mqttCredentials.password), mqttPassword->valuestring);
    }
    copyBounded(g_ctx->draft.adminPassword, sizeof(g_ctx->draft.adminPassword),
                adminPassword->valuestring);
    cJSON_Delete(root);

    g_ctx->connectorsConfigured = true;
    cJSON* json = cJSON_CreateObject();
    cJSON_AddBoolToObject(json, "ok", true);
    return sendJson(req, json);
}

esp_err_t handleCommit(httpd_req_t* req)
{
    if (!g_ctx->wifiConfigured || !g_ctx->moduleConfigured || !g_ctx->connectorsConfigured) {
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

    if (ConnectorSecretStore::setAdminPassword(g_ctx->draft.adminPassword) != ESP_OK) {
        return sendError(req, 500, "admin_password_save_failed");
    }
    const esp_err_t mqttSecretError =
        config.connectors.mqttEnabled
            ? ConnectorSecretStore::saveMqttCredentials(g_ctx->draft.mqttCredentials)
            : ConnectorSecretStore::clearMqttCredentials();
    if (mqttSecretError != ESP_OK) {
        return sendError(req, 500, "mqtt_credentials_save_failed");
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
    // Phones open many keep-alive connections; recycle the oldest instead of refusing new ones.
    config.lru_purge_enable = true;

    if (httpd_start(&ctx.server, &config) != ESP_OK) {
        return ESP_FAIL;
    }

    const httpd_uri_t routeRoot = {"/", HTTP_GET, handleRoot, nullptr};
    const httpd_uri_t routeCss = {"/css/app.css", HTTP_GET, handleCss, nullptr};
    const httpd_uri_t routeJs = {"/js/app.js", HTTP_GET, handleJs, nullptr};
    const httpd_uri_t routeFont = {"/fonts/geist.woff2", HTTP_GET, handleFont, nullptr};
    const httpd_uri_t routeSession = {"/api/v1/session", HTTP_GET, handleSession, nullptr};
    const httpd_uri_t routeScan = {"/api/v1/wifi/networks", HTTP_GET, handleWifiScan, nullptr};
    const httpd_uri_t routeWifi = {"/api/v1/wifi", HTTP_PUT, handleWifiPut, nullptr};
    const httpd_uri_t routeSchema = {"/api/v1/setup/schema", HTTP_GET, handleSetupSchema, nullptr};
    const httpd_uri_t routeModule = {"/api/v1/setup/module", HTTP_PUT, handleSetupPut, nullptr};
    const httpd_uri_t routeConnectors = {"/api/v1/setup/connectors", HTTP_PUT,
                                         handleConnectorsPut, nullptr};
    const httpd_uri_t routeCommit = {"/api/v1/setup/commit", HTTP_POST, handleCommit, nullptr};
    const httpd_uri_t route204 = {"/generate_204", HTTP_GET, handleCaptiveProbe, nullptr};
    const httpd_uri_t routeApple = {"/hotspot-detect.html", HTTP_GET, handleCaptiveProbe, nullptr};
    const httpd_uri_t routeMs = {"/connecttest.txt", HTTP_GET, handleCaptiveProbe, nullptr};

    httpd_register_uri_handler(ctx.server, &routeRoot);
    httpd_register_uri_handler(ctx.server, &routeCss);
    httpd_register_uri_handler(ctx.server, &routeJs);
    httpd_register_uri_handler(ctx.server, &routeFont);
    httpd_register_uri_handler(ctx.server, &routeSession);
    httpd_register_uri_handler(ctx.server, &routeScan);
    httpd_register_uri_handler(ctx.server, &routeWifi);
    httpd_register_uri_handler(ctx.server, &routeSchema);
    httpd_register_uri_handler(ctx.server, &routeModule);
    httpd_register_uri_handler(ctx.server, &routeConnectors);
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
    ctx.dnsAlive = true;
    if (xTaskCreate(dnsTask, "dns_hijack", 3072, &ctx, 3, nullptr) != pdPASS) ctx.dnsAlive = false;

    if (startHttpServer(ctx) != ESP_OK) {
        ESP_LOGE(kTag, "HTTP server start failed");
        stopDns(ctx);
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
    stopDns(ctx);
    WiFiService::stopSoftAp();

    outConfig = ctx.result;
    const bool ok = ctx.success;
    g_ctx = nullptr;
    return ok;
}

}  // namespace opencomfort
