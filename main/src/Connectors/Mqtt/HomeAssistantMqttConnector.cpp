#include "src/Connectors/Mqtt/HomeAssistantMqttConnector.hpp"

#include "src/Config/DeviceHostname.hpp"
#include "src/Schedule/ScheduleService.hpp"
#include "src/Update/UpdateService.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>

#include "sdkconfig.h"

extern "C" {
#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "esp_app_desc.h"
#include "esp_mac.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
}

namespace opencomfort {
namespace {

constexpr const char* kTag = "HomeAssistantMqtt";

struct SleepOption {
    const char* label;
    uint32_t minutes;
};
constexpr SleepOption kSleepOptions[] = {{"Off", 0}, {"30 min", 30}, {"1 h", 60}, {"2 h", 120}, {"4 h", 240}};

// The sleep timer counts down, so show the smallest option that still covers what is left.
const char* sleepLabel(uint32_t secondsLeft)
{
    if (secondsLeft == 0) return kSleepOptions[0].label;
    for (const SleepOption& option : kSleepOptions) {
        if (option.minutes * 60 >= secondsLeft) return option.label;
    }
    return kSleepOptions[4].label;
}

// True when `topic` is "<base>/<suffix>".
bool topicIs(const char* topic, int length, const char* base, const char* suffix)
{
    const size_t baseLength = std::strlen(base);
    const size_t suffixLength = std::strlen(suffix);
    return static_cast<size_t>(length) == baseLength + 1 + suffixLength &&
           std::strncmp(topic, base, baseLength) == 0 && topic[baseLength] == '/' &&
           std::strncmp(topic + baseLength + 1, suffix, suffixLength) == 0;
}

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
    pendingEvent_ = Event::None;  // an event from the old client must not touch a new one
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
    switch (pendingEvent_.exchange(Event::None)) {
        case Event::Connected:
            onConnected();
            break;
        case Event::Disconnected:
            status_.state = ConnectorState::Degraded;
            status_.lastError = ESP_ERR_INVALID_STATE;
            reconnectAt_ = now + pdMS_TO_TICKS(reconnectDelayMs_);
            break;
        case Event::None:
            break;
    }
    if (clientError_.exchange(false)) status_.lastError = ESP_FAIL;
    if (status_.state == ConnectorState::Running && republish_.exchange(false)) {
        publishDiscovery();
        publishState(true);
        publishExtras();
    }
    if (extrasDue_.exchange(false)) nextExtrasAt_ = now;
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
        if (static_cast<int32_t>(now - nextExtrasAt_) >= 0) {
            nextExtrasAt_ = now + pdMS_TO_TICKS(15000);
            publishExtras();
        }
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
            pendingEvent_ = Event::Connected;
            break;
        case MQTT_EVENT_DISCONNECTED:
            pendingEvent_ = Event::Disconnected;
            break;
        case MQTT_EVENT_DATA:
            onData(event->topic, event->topic_len, event->data, event->data_len);
            break;
        case MQTT_EVENT_ERROR:
            clientError_ = true;
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
    char topic[128];
    std::snprintf(topic, sizeof(topic), "%s/+/set", baseTopic_);
    esp_mqtt_client_subscribe(client_, topic, 1);
    std::snprintf(topic, sizeof(topic), "%s/update/install", baseTopic_);
    esp_mqtt_client_subscribe(client_, topic, 1);
    // Home Assistant's birth message: re-announce after it restarts.
    esp_mqtt_client_subscribe(client_, haStatusTopic_, 1);
    publishDiscovery();
    esp_mqtt_client_publish(client_, availabilityTopic_, "online", 0, 1, 1);
    publishState(true);
    publishExtras();
}

void HomeAssistantMqttConnector::onData(const char* topic, int topicLength, const char* data,
                                        int dataLength)
{
    if (dataLength <= 0 || dataLength > 32 || topic == nullptr) {
        return;
    }
    char value[33] = {};
    std::memcpy(value, data, static_cast<size_t>(dataLength));

    if (static_cast<size_t>(topicLength) == std::strlen(haStatusTopic_) &&
        std::strncmp(topic, haStatusTopic_, static_cast<size_t>(topicLength)) == 0) {
        if (std::strcmp(value, "online") == 0) republish_ = true;
        return;
    }
    if (topicIs(topic, topicLength, baseTopic_, "sleep/set")) {
        for (const SleepOption& option : kSleepOptions) {
            if (std::strcmp(value, option.label) == 0) {
                ScheduleService::instance().setSleepMinutes(option.minutes);
                extrasDue_ = true;  // show the new choice right away
            }
        }
        return;
    }
    if (topicIs(topic, topicLength, baseTopic_, "update/install")) {
        if (std::strcmp(value, "install") == 0) (void)UpdateService::instance().installNow();
        extrasDue_ = true;
        return;
    }

    AcDesiredState desired = runtime_.snapshot().desired;
    bool valid = false;
    if (topicIs(topic, topicLength, baseTopic_, "mode/set")) {
        valid = parseMode(value, desired.mode);
        if (valid) {
            desired.powerOn = desired.mode != AcMode::Off;
        }
    } else if (topicIs(topic, topicLength, baseTopic_, "power/set")) {
        valid = std::strcmp(value, "ON") == 0 || std::strcmp(value, "OFF") == 0;
        if (valid) {
            desired.powerOn = value[1] == 'N';
            // Turning on keeps the last mode; "off" is not a mode to resume into.
            if (desired.powerOn && desired.mode == AcMode::Off) desired.mode = AcMode::Auto;
        }
    } else if (topicIs(topic, topicLength, baseTopic_, "fan/set")) {
        valid = parseFan(value, desired.fan);
    } else if (topicIs(topic, topicLength, baseTopic_, "swing/set")) {
        const AcRuntimeCapabilities caps = runtime_.snapshot().acCapabilities;
        for (size_t i = 0; i < caps.vaneCount; ++i) {
            if (std::strcmp(caps.vaneNames[i], value) == 0) {
                desired.vanes = static_cast<uint8_t>(i);
                valid = true;
            }
        }
    } else if (topicIs(topic, topicLength, baseTopic_, "temperature/set")) {
        char* end = nullptr;
        const float temperature = std::strtof(value, &end);
        valid = end != value && *end == '\0';
        if (valid) {
            desired.targetTempC = static_cast<float>(static_cast<int>(temperature + 0.5f));
        }
    }
    if (valid) {
        (void)runtime_.requestDesiredState(desired, RuntimeCommandSource::HomeAssistantMqtt);
    }
}

void HomeAssistantMqttConnector::publishEntity(const char* component, const char* object, cJSON* json)
{
    // Shared fields: "~" abbreviates the base topic; identifiers attach it to the device.
    cJSON_AddStringToObject(json, "~", baseTopic_);
    cJSON_AddStringToObject(json, "availability_topic", "~/availability");
    if (cJSON_GetObjectItem(json, "device") == nullptr) {
        cJSON* device = cJSON_AddObjectToObject(json, "device");
        cJSON* identifiers = cJSON_AddArrayToObject(device, "identifiers");
        cJSON_AddItemToArray(identifiers, cJSON_CreateString(uniqueId_));
    }
    char topic[192];
    if (object == nullptr) {
        std::snprintf(topic, sizeof(topic), "%s", discoveryTopic_);
    } else {
        std::snprintf(topic, sizeof(topic), "%s/%s/%s/%s/config", config_.connectors.mqttDiscoveryPrefix,
                      component, uniqueId_, object);
    }
    char* payload = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);
    if (payload != nullptr) {
        esp_mqtt_client_publish(client_, topic, payload, 0, 1, 1);
        cJSON_free(payload);
    }
}

void HomeAssistantMqttConnector::publishDiscovery()
{
    const RuntimeSnapshot snapshot = runtime_.snapshot();
    char id[72];
    char hostname[kMaxHostnameLen + 1] = "opencomfort";
    (void)resolveDeviceHostname(config_, hostname, sizeof(hostname));
    char url[kMaxHostnameLen + 16];
    std::snprintf(url, sizeof(url), "http://%s.local/", hostname);

    // Climate: power, mode, whole-degree setpoint, fan and the room temperature.
    cJSON* climate = cJSON_CreateObject();
    cJSON_AddNullToObject(climate, "name");  // the device name
    cJSON_AddStringToObject(climate, "unique_id", uniqueId_);
    cJSON_AddStringToObject(climate, "power_command_topic", "~/power/set");
    cJSON_AddStringToObject(climate, "payload_on", "ON");
    cJSON_AddStringToObject(climate, "payload_off", "OFF");
    cJSON_AddStringToObject(climate, "mode_command_topic", "~/mode/set");
    cJSON_AddStringToObject(climate, "mode_state_topic", "~/mode/state");
    cJSON_AddStringToObject(climate, "temperature_command_topic", "~/temperature/set");
    cJSON_AddStringToObject(climate, "temperature_state_topic", "~/temperature/state");
    cJSON_AddStringToObject(climate, "current_temperature_topic", "~/current/state");
    cJSON_AddStringToObject(climate, "fan_mode_command_topic", "~/fan/set");
    cJSON_AddStringToObject(climate, "fan_mode_state_topic", "~/fan/state");
    cJSON_AddNumberToObject(climate, "min_temp", snapshot.acCapabilities.minTempC);
    cJSON_AddNumberToObject(climate, "max_temp", snapshot.acCapabilities.maxTempC);
    cJSON_AddNumberToObject(climate, "temp_step", 1);
    cJSON_AddNumberToObject(climate, "precision", 1.0);
    cJSON_AddStringToObject(climate, "temperature_unit", "C");
    cJSON* modes = cJSON_AddArrayToObject(climate, "modes");
    for (const char* mode : {"off", "auto", "cool", "heat", "dry", "fan_only"}) {
        cJSON_AddItemToArray(modes, cJSON_CreateString(mode));
    }
    cJSON* fanModes = cJSON_AddArrayToObject(climate, "fan_modes");
    for (const char* fan : {"auto", "low", "medium", "high"}) {
        cJSON_AddItemToArray(fanModes, cJSON_CreateString(fan));
    }
    // Vanes and the live action only when the profile supports them.
    const AcRuntimeCapabilities& caps = snapshot.acCapabilities;
    if (caps.vaneCount > 0) {
        cJSON_AddStringToObject(climate, "swing_mode_command_topic", "~/swing/set");
        cJSON_AddStringToObject(climate, "swing_mode_state_topic", "~/swing/state");
        cJSON* swingModes = cJSON_AddArrayToObject(climate, "swing_modes");
        for (size_t i = 0; i < caps.vaneCount; ++i) {
            cJSON_AddItemToArray(swingModes, cJSON_CreateString(caps.vaneNames[i]));
        }
    }
    if (caps.reportsAction) {
        cJSON_AddStringToObject(climate, "action_topic", "~/action/state");
    }
    cJSON* device = cJSON_AddObjectToObject(climate, "device");
    cJSON* identifiers = cJSON_AddArrayToObject(device, "identifiers");
    cJSON_AddItemToArray(identifiers, cJSON_CreateString(uniqueId_));
    cJSON_AddStringToObject(device, "name", config_.friendlyName);
    cJSON_AddStringToObject(device, "manufacturer", "OpenComfort");
    cJSON_AddStringToObject(device, "model", config_.ac.modelId);
    cJSON_AddStringToObject(device, "sw_version", esp_app_get_description()->version);
    cJSON_AddStringToObject(device, "configuration_url", url);
    publishEntity("climate", nullptr, climate);

    cJSON* room = cJSON_CreateObject();
    cJSON_AddStringToObject(room, "name", "Room temperature");
    std::snprintf(id, sizeof(id), "%s_room", uniqueId_);
    cJSON_AddStringToObject(room, "unique_id", id);
    cJSON_AddStringToObject(room, "state_topic", "~/current/state");
    cJSON_AddStringToObject(room, "device_class", "temperature");
    cJSON_AddStringToObject(room, "state_class", "measurement");
    cJSON_AddStringToObject(room, "unit_of_measurement", "°C");
    publishEntity("sensor", "room", room);

    cJSON* sleep = cJSON_CreateObject();
    cJSON_AddStringToObject(sleep, "name", "Sleep timer");
    std::snprintf(id, sizeof(id), "%s_sleep", uniqueId_);
    cJSON_AddStringToObject(sleep, "unique_id", id);
    cJSON_AddStringToObject(sleep, "command_topic", "~/sleep/set");
    cJSON_AddStringToObject(sleep, "state_topic", "~/sleep/state");
    cJSON_AddStringToObject(sleep, "icon", "mdi:power-sleep");
    cJSON* options = cJSON_AddArrayToObject(sleep, "options");
    for (const SleepOption& option : kSleepOptions) {
        cJSON_AddItemToArray(options, cJSON_CreateString(option.label));
    }
    publishEntity("select", "sleep", sleep);

    cJSON* answering = cJSON_CreateObject();
    cJSON_AddStringToObject(answering, "name", "Unit answering");
    std::snprintf(id, sizeof(id), "%s_answering", uniqueId_);
    cJSON_AddStringToObject(answering, "unique_id", id);
    cJSON_AddStringToObject(answering, "state_topic", "~/answering/state");
    cJSON_AddStringToObject(answering, "device_class", "connectivity");
    cJSON_AddStringToObject(answering, "entity_category", "diagnostic");
    publishEntity("binary_sensor", "answering", answering);

    cJSON* rssi = cJSON_CreateObject();
    cJSON_AddStringToObject(rssi, "name", "Wi-Fi signal");
    std::snprintf(id, sizeof(id), "%s_rssi", uniqueId_);
    cJSON_AddStringToObject(rssi, "unique_id", id);
    cJSON_AddStringToObject(rssi, "state_topic", "~/rssi/state");
    cJSON_AddStringToObject(rssi, "device_class", "signal_strength");
    cJSON_AddStringToObject(rssi, "state_class", "measurement");
    cJSON_AddStringToObject(rssi, "unit_of_measurement", "dBm");
    cJSON_AddStringToObject(rssi, "entity_category", "diagnostic");
    cJSON_AddBoolToObject(rssi, "enabled_by_default", false);
    publishEntity("sensor", "rssi", rssi);

    if (UpdateService::instance().status().enabled) {
        cJSON* update = cJSON_CreateObject();
        cJSON_AddStringToObject(update, "name", "Firmware");
        std::snprintf(id, sizeof(id), "%s_firmware", uniqueId_);
        cJSON_AddStringToObject(update, "unique_id", id);
        cJSON_AddStringToObject(update, "device_class", "firmware");
        cJSON_AddStringToObject(update, "state_topic", "~/update/state");
        cJSON_AddStringToObject(update, "command_topic", "~/update/install");
        cJSON_AddStringToObject(update, "payload_install", "install");
        cJSON_AddStringToObject(update, "entity_category", "config");
        publishEntity("update", "firmware", update);
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
    std::snprintf(number, sizeof(number), "%.0f", snapshot.desired.targetTempC);
    esp_mqtt_client_publish(client_, topic, number, 0, 1, 1);
    std::snprintf(topic, sizeof(topic), "%s/fan/state", baseTopic_);
    esp_mqtt_client_publish(client_, topic, fanName(snapshot.desired.fan), 0, 1, 1);
    const AcRuntimeCapabilities& caps = snapshot.acCapabilities;
    if (caps.vaneCount > 0 && snapshot.desired.vanes < caps.vaneCount) {
        std::snprintf(topic, sizeof(topic), "%s/swing/state", baseTopic_);
        esp_mqtt_client_publish(client_, topic, caps.vaneNames[snapshot.desired.vanes], 0, 1, 1);
    }
    const AcAction action = acAction(snapshot.desired, snapshot.reported);
    if (action != AcAction::Unknown) {
        // Home Assistant's names: off, idle, cooling, heating, drying, fan.
        std::snprintf(topic, sizeof(topic), "%s/action/state", baseTopic_);
        esp_mqtt_client_publish(client_, topic, toString(action), 0, 1, 1);
    }
    if (snapshot.reported.hasRoomTemp) {
        std::snprintf(topic, sizeof(topic), "%s/current/state", baseTopic_);
        std::snprintf(number, sizeof(number), "%.1f", snapshot.reported.returnAirC);
        esp_mqtt_client_publish(client_, topic, number, 0, 1, 1);
    }
    publishedRevision_ = snapshot.revision;
}

// Values that change without a runtime revision: sleep countdown, unit contact, signal, update.
void HomeAssistantMqttConnector::publishExtras()
{
    char topic[128];
    std::snprintf(topic, sizeof(topic), "%s/sleep/state", baseTopic_);
    esp_mqtt_client_publish(client_, topic, sleepLabel(ScheduleService::instance().sleepSecondsLeft()), 0, 1, 1);

    std::snprintf(topic, sizeof(topic), "%s/answering/state", baseTopic_);
    esp_mqtt_client_publish(client_, topic, runtime_.snapshot().reported.valid ? "ON" : "OFF", 0, 1, 1);

    wifi_ap_record_t ap{};
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        char number[8];
        std::snprintf(number, sizeof(number), "%d", ap.rssi);
        std::snprintf(topic, sizeof(topic), "%s/rssi/state", baseTopic_);
        esp_mqtt_client_publish(client_, topic, number, 0, 0, 0);
    }

    const UpdateStatus update = UpdateService::instance().status();
    if (update.enabled) {
        const bool newer = update.state == UpdateState::Available || update.state == UpdateState::Downloading;
        char payload[192];
        if (update.state == UpdateState::Downloading) {
            std::snprintf(payload, sizeof(payload),
                          "{\"installed_version\":\"%s\",\"latest_version\":\"%s\",\"in_progress\":true,\"update_percentage\":%u}",
                          update.current, update.latest, static_cast<unsigned>(update.progress));
        } else {
            std::snprintf(payload, sizeof(payload),
                          "{\"installed_version\":\"%s\",\"latest_version\":\"%s\",\"in_progress\":false}",
                          update.current, newer ? update.latest : update.current);
        }
        std::snprintf(topic, sizeof(topic), "%s/update/state", baseTopic_);
        esp_mqtt_client_publish(client_, topic, payload, 0, 1, 1);
    }
}

void HomeAssistantMqttConnector::buildIdentity(const UserConfig& config)
{
    uint8_t mac[6] = {};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    std::snprintf(uniqueId_, sizeof(uniqueId_), "opencomfort_%02x%02x%02x%02x%02x%02x", mac[0],
                  mac[1], mac[2], mac[3], mac[4], mac[5]);
    std::snprintf(baseTopic_, sizeof(baseTopic_), "opencomfort/%s", uniqueId_);
    std::snprintf(availabilityTopic_, sizeof(availabilityTopic_), "%s/availability", baseTopic_);
    std::snprintf(discoveryTopic_, sizeof(discoveryTopic_), "%s/climate/%s/config",
                  config.connectors.mqttDiscoveryPrefix, uniqueId_);
    std::snprintf(haStatusTopic_, sizeof(haStatusTopic_), "%s/status", config.connectors.mqttDiscoveryPrefix);
}

}  // namespace opencomfort
