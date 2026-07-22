#include "unity.h"

#include "src/Connectors/ConnectorManager.hpp"

namespace opencomfort {
namespace {

class MockConnector final : public IConnector {
public:
    explicit MockConnector(bool useMqttFlag) : useMqttFlag_(useMqttFlag) {}

    const char* id() const override { return "mock"; }
    bool enabled(const ConnectorConfig& config) const override
    {
        return !useMqttFlag_ || config.mqttEnabled;
    }
    esp_err_t start(const UserConfig&) override
    {
        starts++;
        state = ConnectorState::Running;
        return startResult;
    }
    void stop() override
    {
        stops++;
        state = ConnectorState::Stopped;
    }
    esp_err_t reconfigure(const UserConfig&) override
    {
        reconfigures++;
        return ESP_OK;
    }
    void tick() override { ticks++; }
    ConnectorStatus status() const override { return {"mock", state, startResult, 0}; }

    int starts = 0;
    int stops = 0;
    int reconfigures = 0;
    int ticks = 0;
    esp_err_t startResult = ESP_OK;
    ConnectorState state = ConnectorState::Stopped;

private:
    bool useMqttFlag_;
};

}  // namespace

TEST_CASE("connector manager starts enabled connectors and stops disabled connectors",
          "[connectors]")
{
    ConnectorManager manager;
    auto web = std::make_unique<MockConnector>(false);
    auto mqtt = std::make_unique<MockConnector>(true);
    MockConnector* webPointer = web.get();
    MockConnector* mqttPointer = mqtt.get();
    TEST_ASSERT_TRUE(manager.add(std::move(web)));
    TEST_ASSERT_TRUE(manager.add(std::move(mqtt)));

    UserConfig config;
    config.connectors.mqttEnabled = false;
    TEST_ASSERT_EQUAL(ESP_OK, manager.start(config));
    TEST_ASSERT_EQUAL(1, webPointer->starts);
    TEST_ASSERT_EQUAL(0, mqttPointer->starts);
    TEST_ASSERT_EQUAL(1, mqttPointer->stops);

    config.connectors.mqttEnabled = true;
    TEST_ASSERT_EQUAL(ESP_OK, manager.reconfigure(config));
    TEST_ASSERT_EQUAL(1, webPointer->reconfigures);
    TEST_ASSERT_EQUAL(1, mqttPointer->reconfigures);
}

}  // namespace opencomfort
