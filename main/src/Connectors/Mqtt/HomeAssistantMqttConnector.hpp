#pragma once

#include "mqtt_client.h"

#include "src/Config/ConnectorSecretStore.hpp"
#include "src/Connectors/IConnector.hpp"
#include "src/Runtime/RuntimeController.hpp"

namespace opencomfort {

class HomeAssistantMqttConnector final : public IConnector {
public:
    explicit HomeAssistantMqttConnector(RuntimeController& runtime);
    ~HomeAssistantMqttConnector() override;

    const char* id() const override;
    bool enabled(const ConnectorConfig& config) const override;
    esp_err_t start(const UserConfig& config) override;
    void stop() override;
    esp_err_t reconfigure(const UserConfig& config) override;
    void tick() override;
    ConnectorStatus status() const override;

private:
    static void eventHandler(void* argument, esp_event_base_t eventBase, int32_t eventId,
                             void* eventData);
    void onEvent(esp_mqtt_event_handle_t event);
    void onConnected();
    void onData(const char* topic, int topicLength, const char* data, int dataLength);
    void publishDiscovery();
    void publishState(bool force);
    void buildIdentity(const UserConfig& config);

    RuntimeController& runtime_;
    UserConfig config_{};
    MqttCredentials credentials_{};
    esp_mqtt_client_handle_t client_ = nullptr;
    ConnectorStatus status_{"home_assistant_mqtt", ConnectorState::Stopped, ESP_OK, 0};
    char uri_[192] = {};
    char baseTopic_[96] = {};
    char availabilityTopic_[128] = {};
    char modeCommandTopic_[128] = {};
    char temperatureCommandTopic_[128] = {};
    char fanCommandTopic_[128] = {};
    char discoveryTopic_[160] = {};
    char uniqueId_[48] = {};
    uint32_t publishedRevision_ = 0;
    TickType_t reconnectAt_ = 0;
    uint32_t reconnectDelayMs_ = 1000;
};

}  // namespace opencomfort
