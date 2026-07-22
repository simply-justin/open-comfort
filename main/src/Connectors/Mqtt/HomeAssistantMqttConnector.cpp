#include "src/Connectors/Mqtt/HomeAssistantMqttConnector.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>

#include "sdkconfig.h"

extern "C" {
#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
}

namespace opencomfort {
namespace {

constexpr const char* kTag = "HomeAssistantMqtt";

const char* modeName(AcMode mode)
{
    switch (mode) {
        case AcMode::Off: return "off";
        case AcMode::Cool: return "cool";
        case AcMode::Heat: return "heat";
        case AcMode::Dry: return "dry";
        case AcMode::Fan: return "fan_only";
        case AcMode::Auto: return "auto";
        default: return "off";
    }
}

bool parseMode(const char* value, AcMode& mode)
{
    if (std::strcmp(value, "off") == 0) mode = AcMode::Off;
    else if (std::strcmp(value, "cool") == 0) mode = AcMode::Cool;
    else if (std::strcmp(value, "heat") == 0) mode = AcMode::Heat;
    else if (std::strcmp(value, "dry") == 0) mode = AcMode::Dry;
    else if (std::strcmp(value, "fan_only") == 0 || std::strcmp(value, "fan") == 0) {
        mode = AcMode::Fan;
    } else if (std::strcmp(value, "auto") == 0) mode = AcMode::Auto;
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
        default: return "auto";
    }
}

bool parseFan(const char* value, AcFanSpeed& fan)
{
    if (std::strcmp(value, "auto") == 0) fan = AcFanSpeed::Auto;
    else if (std::strcmp(value, "low") == 0) fan = AcFanSpeed::Low;
    else if (std::strcmp(value, "medium") == 0) fan = AcFanSpeed::Medium;
    else if (std::strcmp(value, "high") == 0) fan = AcFanSpeed::High;
    else return false;
    return true;
}

bool topicEquals(const char* topic, int topicLength, const char* expected)
{
    const size_t expectedLength = std::strlen(expected);
    return topicLength == static_cast<int>(expectedLength) &&
           std::memcmp(topic, expected, expectedLength) == 0;
}

}  // namespace

HomeAssistantMqttConnector::HomeAssistantMqttConnector(RuntimeController& runtime)
    : runtime_(runtime)
{
}

HomeAssistantMqttConnector::~HomeAssistantMqttConnector()
{
    stop();
}

const char* HomeAssistantMqttConnector::id() const
{
    return "home_assistant_mqtt";
}

bool HomeAssistantMqttConnector::enabled(const ConnectorConfig& config) const
{
    return config.mqttEnabled;
}

esp_err_t HomeAssistantMqttConnector::start(const UserConfig& config)
{
    if (!config.connectors.mqttEnabled) {
        status_.state = ConnectorState::Disabled;
        return ESP_OK;
    }
    if (client_ != nullptr) {
        return ESP_OK;
    }

    config_ = config;
    credentials_ = {};
    const esp_err_t secretError = ConnectorSecretStore::loadMqttCredentials(credentials_);
    if (secretError != ESP_OK && secretError != ESP_ERR_NVS_NOT_FOUND) {
        status_ = {"home_assistant_mqtt", ConnectorState::Degraded, secretError, 0};
        return secretError;
    }
    buildIdentity(config);

    std::snprintf(uri_, sizeof(uri_), "%s://%s:%u", config.connectors.mqttTls ? "mqtts" : "mqtt",
                  config.connectors.mqttHost,
                  static_cast<unsigned>(config.connectors.mqttPort));

    esp_mqtt_client_config_t mqttConfig = {};
    mqttConfig.broker.address.uri = uri_;
    if (config.connectors.mqttTls) {
        mqttConfig.broker.verification.crt_bundle_attach = esp_crt_bundle_attach;
    }
    mqttConfig.credentials.client_id = uniqueId_;
    if (credentials_.username[0] != '\0') {
        mqttConfig.credentials.username = credentials_.username;
    }
    if (credentials_.password[0] != '\0') {
        mqttConfig.credentials.authentication.password = credentials_.password;
    }
    mqttConfig.session.last_will.topic = availabilityTopic_;
    mqttConfig.session.last_will.msg = "offline";
    mqttConfig.session.last_will.msg_len = 7;
    mqttConfig.session.last_will.qos = 1;
    mqttConfig.session.last_will.retain = true;
    mqttConfig.network.disable_auto_reconnect = true;

    client_ = esp_mqtt_client_init(&mqttConfig);
    if (client_ == nullptr) {
        status_ = {"home_assistant_mqtt", ConnectorState::Degraded, ESP_ERR_NO_MEM, 0};
        return ESP_ERR_NO_MEM;
    }
    esp_err_t err =
        esp_mqtt_client_register_event(client_, MQTT_EVENT_ANY, eventHandler, this);
    if (err == ESP_OK) {
        err = esp_mqtt_client_start(client_);
    }
    if (err != ESP_OK) {
        esp_mqtt_client_destroy(client_);
        client_ = nullptr;
        status_ = {"home_assistant_mqtt", ConnectorState::Degraded, err, 0};
        return err;
    }
    status_ = {"home_assistant_mqtt", ConnectorState::Starting, ESP_OK, 0};
    reconnectDelayMs_ = 1000;
    return ESP_OK;
}

void HomeAssistantMqttConnector::stop()
{
    if (client_ != nullptr) {
        if (status_.state == ConnectorState::Running) {
            esp_mqtt_client_publish(client_, availabilityTopic_, "offline", 0, 1, 1);
        }
        esp_mqtt_client_stop(client_);
        esp_mqtt_client_destroy(client_);
        client_ = nullptr;
    }
    status_.state = ConnectorState::Stopped;
    reconnectAt_ = 0;
}

esp_err_t HomeAssistantMqttConnector::reconfigure(const UserConfig& config)
{
    stop();
    return start(config);
}

void HomeAssistantMqttConnector::tick()
{
    if (client_ == nullptr) {
        return;
    }
    const TickType_t now = xTaskGetTickCount();
    if (status_.state == ConnectorState::Degraded && reconnectAt_ != 0 &&
        static_cast<int32_t>(now - reconnectAt_) >= 0) {
        const esp_err_t err = esp_mqtt_client_reconnect(client_);
        status_.lastError = err;
        status_.state = err == ESP_OK ? ConnectorState::Starting : ConnectorState::Degraded;
        status_.reconnectCount++;
        reconnectAt_ = err == ESP_OK ? 0 : now + pdMS_TO_TICKS(reconnectDelayMs_);
        reconnectDelayMs_ *= 2;
        if (reconnectDelayMs_ > CONFIG_OPENCOMFORT_MQTT_RECONNECT_MAX_MS) {
            reconnectDelayMs_ = CONFIG_OPENCOMFORT_MQTT_RECONNECT_MAX_MS;
        }
    }
    if (status_.state == ConnectorState::Running) {
        publishState(false);
    }
}

ConnectorStatus HomeAssistantMqttConnector::status() const
{
    return status_;
}

void HomeAssistantMqttConnector::eventHandler(void* argument, esp_event_base_t, int32_t,
                                              void* eventData)
{
    static_cast<HomeAssistantMqttConnector*>(argument)->onEvent(
        static_cast<esp_mqtt_event_handle_t>(eventData));
}

void HomeAssistantMqttConnector::onEvent(esp_mqtt_event_handle_t event)
{
    switch (static_cast<esp_mqtt_event_id_t>(event->event_id)) {
        case MQTT_EVENT_CONNECTED:
            onConnected();
            break;
        case MQTT_EVENT_DISCONNECTED:
            status_.state = ConnectorState::Degraded;
            status_.lastError = ESP_ERR_INVALID_STATE;
            reconnectAt_ = xTaskGetTickCount() + pdMS_TO_TICKS(reconnectDelayMs_);
            break;
        case MQTT_EVENT_DATA:
            onData(event->topic, event->topic_len, event->data, event->data_len);
            break;
        case MQTT_EVENT_ERROR:
            status_.lastError = ESP_FAIL;
            break;
        default:
            break;
    }
}

void HomeAssistantMqttConnector::onConnected()
{
    status_.state = ConnectorState::Running;
    status_.lastError = ESP_OK;
    reconnectAt_ = 0;
    reconnectDelayMs_ = 1000;
    esp_mqtt_client_subscribe(client_, modeCommandTopic_, 1);
    esp_mqtt_client_subscribe(client_, temperatureCommandTopic_, 1);
    esp_mqtt_client_subscribe(client_, fanCommandTopic_, 1);
    publishDiscovery();
    esp_mqtt_client_publish(client_, availabilityTopic_, "online", 0, 1, 1);
    publishState(true);
}

void HomeAssistantMqttConnector::onData(const char* topic, int topicLength, const char* data,
                                        int dataLength)
{
    if (dataLength <= 0 || dataLength > 32) {
        return;
    }
    char value[33] = {};
    std::memcpy(value, data, static_cast<size_t>(dataLength));
    AcDesiredState desired = runtime_.snapshot().desired;
    bool valid = false;
    if (topicEquals(topic, topicLength, modeCommandTopic_)) {
        valid = parseMode(value, desired.mode);
        if (valid) {
            desired.powerOn = desired.mode != AcMode::Off;
        }
    } else if (topicEquals(topic, topicLength, fanCommandTopic_)) {
        valid = parseFan(value, desired.fan);
    } else if (topicEquals(topic, topicLength, temperatureCommandTopic_)) {
        char* end = nullptr;
        const float temperature = std::strtof(value, &end);
        valid = end != value && *end == '\0';
        if (valid) {
            desired.targetTempC = temperature;
        }
    }
    if (valid) {
        (void)runtime_.requestDesiredState(desired, RuntimeCommandSource::HomeAssistantMqtt);
    }
}

void HomeAssistantMqttConnector::publishDiscovery()
{
    const RuntimeSnapshot snapshot = runtime_.snapshot();
    char modeStateTopic[128];
    char temperatureStateTopic[128];
    char currentTemperatureTopic[128];
    char fanStateTopic[128];
    std::snprintf(modeStateTopic, sizeof(modeStateTopic), "%s/mode/state", baseTopic_);
    std::snprintf(temperatureStateTopic, sizeof(temperatureStateTopic), "%s/temperature/state",
                  baseTopic_);
    std::snprintf(currentTemperatureTopic, sizeof(currentTemperatureTopic), "%s/current/state",
                  baseTopic_);
    std::snprintf(fanStateTopic, sizeof(fanStateTopic), "%s/fan/state", baseTopic_);

    cJSON* json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "name", config_.friendlyName);
    cJSON_AddStringToObject(json, "unique_id", uniqueId_);
    cJSON_AddStringToObject(json, "availability_topic", availabilityTopic_);
    cJSON_AddStringToObject(json, "mode_command_topic", modeCommandTopic_);
    cJSON_AddStringToObject(json, "mode_state_topic", modeStateTopic);
    cJSON_AddStringToObject(json, "temperature_command_topic", temperatureCommandTopic_);
    cJSON_AddStringToObject(json, "temperature_state_topic", temperatureStateTopic);
    cJSON_AddStringToObject(json, "current_temperature_topic", currentTemperatureTopic);
    cJSON_AddStringToObject(json, "fan_mode_command_topic", fanCommandTopic_);
    cJSON_AddStringToObject(json, "fan_mode_state_topic", fanStateTopic);
    cJSON_AddNumberToObject(json, "min_temp", snapshot.acCapabilities.minTempC);
    cJSON_AddNumberToObject(json, "max_temp", snapshot.acCapabilities.maxTempC);
    cJSON_AddNumberToObject(json, "temp_step", 0.5);
    cJSON_AddStringToObject(json, "temperature_unit", "C");

    cJSON* modes = cJSON_AddArrayToObject(json, "modes");
    for (const char* mode : {"off", "cool", "heat", "dry", "fan_only", "auto"}) {
        cJSON_AddItemToArray(modes, cJSON_CreateString(mode));
    }
    cJSON* fanModes = cJSON_AddArrayToObject(json, "fan_modes");
    for (const char* fan : {"auto", "low", "medium", "high"}) {
        cJSON_AddItemToArray(fanModes, cJSON_CreateString(fan));
    }
    cJSON* device = cJSON_AddObjectToObject(json, "device");
    cJSON* identifiers = cJSON_AddArrayToObject(device, "identifiers");
    cJSON_AddItemToArray(identifiers, cJSON_CreateString(uniqueId_));
    cJSON_AddStringToObject(device, "name", config_.friendlyName);
    cJSON_AddStringToObject(device, "manufacturer", "OpenComfort");
    cJSON_AddStringToObject(device, "model", config_.ac.modelId);

    char* payload = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);
    if (payload != nullptr) {
        esp_mqtt_client_publish(client_, discoveryTopic_, payload, 0, 1, 1);
        cJSON_free(payload);
    }
}

void HomeAssistantMqttConnector::publishState(bool force)
{
    const RuntimeSnapshot snapshot = runtime_.snapshot();
    if (!force && snapshot.revision == publishedRevision_) {
        return;
    }
    char topic[128];
    char number[24];
    std::snprintf(topic, sizeof(topic), "%s/mode/state", baseTopic_);
    esp_mqtt_client_publish(client_, topic, modeName(snapshot.desired.powerOn ? snapshot.desired.mode
                                                                             : AcMode::Off),
                            0, 1, 1);
    std::snprintf(topic, sizeof(topic), "%s/temperature/state", baseTopic_);
    std::snprintf(number, sizeof(number), "%.1f", snapshot.desired.targetTempC);
    esp_mqtt_client_publish(client_, topic, number, 0, 1, 1);
    std::snprintf(topic, sizeof(topic), "%s/fan/state", baseTopic_);
    esp_mqtt_client_publish(client_, topic, fanName(snapshot.desired.fan), 0, 1, 1);
    if (snapshot.reported.valid) {
        std::snprintf(topic, sizeof(topic), "%s/current/state", baseTopic_);
        std::snprintf(number, sizeof(number), "%.1f", snapshot.reported.returnAirC);
        esp_mqtt_client_publish(client_, topic, number, 0, 1, 1);
    }
    publishedRevision_ = snapshot.revision;
}

void HomeAssistantMqttConnector::buildIdentity(const UserConfig& config)
{
    uint8_t mac[6] = {};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    std::snprintf(uniqueId_, sizeof(uniqueId_), "opencomfort_%02x%02x%02x%02x%02x%02x", mac[0],
                  mac[1], mac[2], mac[3], mac[4], mac[5]);
    std::snprintf(baseTopic_, sizeof(baseTopic_), "opencomfort/%s", uniqueId_);
    std::snprintf(availabilityTopic_, sizeof(availabilityTopic_), "%s/availability", baseTopic_);
    std::snprintf(modeCommandTopic_, sizeof(modeCommandTopic_), "%s/mode/set", baseTopic_);
    std::snprintf(temperatureCommandTopic_, sizeof(temperatureCommandTopic_),
                  "%s/temperature/set", baseTopic_);
    std::snprintf(fanCommandTopic_, sizeof(fanCommandTopic_), "%s/fan/set", baseTopic_);
    std::snprintf(discoveryTopic_, sizeof(discoveryTopic_), "%s/climate/%s/config",
                  config.connectors.mqttDiscoveryPrefix, uniqueId_);
}

}  // namespace opencomfort
