#include "src/Http/WebPortal.hpp"

extern "C" {
    #include "cJSON.h"
    #include "esp_log.h"
    #include "esp_system.h"
}

#include <cstdio>
#include <string>

#include "src/Configs/ConfigManager.hpp"
#include "src/Modules/Ac/Ac.hpp"
#include "src/Http/HttpBody.hpp"
#include "src/Http/HttpPages.hpp"

static const char* TAG = "WebPortal";

static esp_err_t sendJson(httpd_req_t* req, const std::string& body, const char* status = nullptr) {
    httpd_resp_set_type(req, "application/json");
    if (status) httpd_resp_set_status(req, status);
    return httpd_resp_send(req, body.c_str(), body.size());
}

static const char* modeToString(Mode mode) {
    switch (mode) {
        case Mode::Off: return "off";
        case Mode::Cool: return "cool";
        case Mode::Heat: return "heat";
        case Mode::Dry: return "dry";
        case Mode::Fan: return "fan";
        case Mode::Auto: return "auto";
    }
    return "unknown";
}

static const char* fanSpeedToString(FanSpeed speed) {
    switch (speed) {
        case FanSpeed::Auto: return "auto";
        case FanSpeed::Low: return "low";
        case FanSpeed::Med: return "medium";
        case FanSpeed::High: return "high";
    }
    return "unknown";
}

static bool parseMode(const std::string& value, Mode& mode) {
    if (value == "off") mode = Mode::Off;
    else if (value == "cool") mode = Mode::Cool;
    else if (value == "heat") mode = Mode::Heat;
    else if (value == "dry") mode = Mode::Dry;
    else if (value == "fan") mode = Mode::Fan;
    else if (value == "auto") mode = Mode::Auto;
    else return false;
    return true;
}

static bool parseFanSpeed(const std::string& value, FanSpeed& speed) {
    if (value == "auto") speed = FanSpeed::Auto;
    else if (value == "low") speed = FanSpeed::Low;
    else if (value == "medium" || value == "med") speed = FanSpeed::Med;
    else if (value == "high") speed = FanSpeed::High;
    else return false;
    return true;
}

WebPortal::WebPortal(const DeviceConfig& config, IModule* module)
    : config_(config), module_(module)
{}

bool WebPortal::start() {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.uri_match_fn = httpd_uri_match_wildcard;

    if (httpd_start(&server_, &config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start web portal");
        return false;
    }

    httpd_uri_t routes[] = {
        {.uri = "/", .method = HTTP_GET, .handler = rootHandler, .user_ctx = this},
        {.uri = "/api/status", .method = HTTP_GET, .handler = statusHandler, .user_ctx = this},
        {.uri = "/api/modules", .method = HTTP_GET, .handler = modulesHandler, .user_ctx = this},
        {.uri = "/api/modules/ac", .method = HTTP_GET, .handler = acHandler, .user_ctx = this},
        {.uri = "/api/modules/ac/state", .method = HTTP_GET, .handler = acStateHandler, .user_ctx = this},
        {.uri = "/api/modules/ac/command", .method = HTTP_POST, .handler = acCommandHandler, .user_ctx = this},
        {.uri = "/api/setup/reset", .method = HTTP_POST, .handler = setupResetHandler, .user_ctx = this},
    };

    for (auto& route : routes) {
        httpd_register_uri_handler(server_, &route);
    }

    ESP_LOGI(TAG, "Web portal started");
    return true;
}

void WebPortal::stop() {
    if (server_) {
        httpd_stop(server_);
        server_ = nullptr;
    }
}

esp_err_t WebPortal::rootHandler(httpd_req_t* req) {
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, HttpPages::kControlPage, HTTPD_RESP_USE_STRLEN);
}

esp_err_t WebPortal::statusHandler(httpd_req_t* req) {
    auto* portal = static_cast<WebPortal*>(req->user_ctx);
    std::string body = "{\"device\":\"" + portal->config_.friendlyName + "\",\"configured\":true}";
    return sendJson(req, body);
}

esp_err_t WebPortal::modulesHandler(httpd_req_t* req) {
    return sendJson(req, "{\"modules\":[\"ac\"]}");
}

esp_err_t WebPortal::acHandler(httpd_req_t* req) {
    auto* portal = static_cast<WebPortal*>(req->user_ctx);
    Ac* ac = dynamic_cast<Ac*>(portal->module_);
    if (!ac) return sendJson(req, "{\"error\":\"AC module unavailable\"}", "404 Not Found");

    std::string body = "{\"type\":\"ac\",\"profile\":\"" + ac->config().acProfilePath + "\"}";
    return sendJson(req, body);
}

esp_err_t WebPortal::acStateHandler(httpd_req_t* req) {
    auto* portal = static_cast<WebPortal*>(req->user_ctx);
    Ac* ac = dynamic_cast<Ac*>(portal->module_);
    if (!ac) return sendJson(req, "{\"error\":\"AC module unavailable\"}", "404 Not Found");

    const State& state = ac->state();
    char body[256];
    std::snprintf(
        body,
        sizeof(body),
        "{\"powerOn\":%s,\"mode\":\"%s\",\"currentTemp\":%.1f,\"targetTemp\":%.1f,\"fanSpeed\":\"%s\"}",
        state.powerOn ? "true" : "false",
        modeToString(state.mode),
        state.currentTemp,
        state.targetTemp,
        fanSpeedToString(state.fanSpeed)
    );
    return sendJson(req, body);
}

esp_err_t WebPortal::acCommandHandler(httpd_req_t* req) {
    auto* portal = static_cast<WebPortal*>(req->user_ctx);
    Ac* ac = dynamic_cast<Ac*>(portal->module_);
    if (!ac) return sendJson(req, "{\"error\":\"AC module unavailable\"}", "404 Not Found");
    std::string body;
    if (!HttpBody::read(req, body, 1024)) {
        return sendJson(req, "{\"error\":\"invalid request size or failed to read body\"}", "400 Bad Request");
    }

    cJSON* root = cJSON_Parse(body.c_str());
    if (!root) return sendJson(req, "{\"error\":\"invalid JSON\"}", "400 Bad Request");

    AcCommand command;
    cJSON* power = cJSON_GetObjectItem(root, "powerOn");
    if (cJSON_IsBool(power)) {
        command.hasPower = true;
        command.powerOn = cJSON_IsTrue(power);
    }

    cJSON* mode = cJSON_GetObjectItem(root, "mode");
    if (cJSON_IsString(mode)) {
        command.hasMode = true;
        if (!parseMode(mode->valuestring, command.mode)) {
            cJSON_Delete(root);
            return sendJson(req, "{\"error\":\"unsupported mode\"}", "400 Bad Request");
        }
    }

    cJSON* targetTemp = cJSON_GetObjectItem(root, "targetTemp");
    if (cJSON_IsNumber(targetTemp)) {
        command.hasTargetTemp = true;
        command.targetTemp = static_cast<float>(targetTemp->valuedouble);
    }

    cJSON* fanSpeed = cJSON_GetObjectItem(root, "fanSpeed");
    if (cJSON_IsString(fanSpeed)) {
        command.hasFanSpeed = true;
        if (!parseFanSpeed(fanSpeed->valuestring, command.fanSpeed)) {
            cJSON_Delete(root);
            return sendJson(req, "{\"error\":\"unsupported fanSpeed\"}", "400 Bad Request");
        }
    }

    std::string error;
    bool ok = ac->applyCommand(command, &error);
    cJSON_Delete(root);
    if (!ok) {
        std::string response = "{\"error\":\"" + error + "\"}";
        return sendJson(req, response, "400 Bad Request");
    }
    return sendJson(req, "{\"ok\":true}");
}

esp_err_t WebPortal::setupResetHandler(httpd_req_t* req) {
    ConfigManager::writeTextFile(ConfigManager::configPath(), "");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":true,\"restarting\":true}");
    esp_restart();
    return ESP_OK;
}
