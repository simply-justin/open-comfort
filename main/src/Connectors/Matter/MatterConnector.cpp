#include "src/Connectors/Matter/MatterConnector.hpp"

#include "src/Connectors/Matter/MatterPlatform.hpp"

namespace opencomfort {

MatterConnector::MatterConnector(RuntimeController& runtime) : runtime_(runtime) {}

MatterConnector::~MatterConnector()
{
    stop();
}

const char* MatterConnector::id() const
{
    return "matter";
}

bool MatterConnector::enabled(const ConnectorConfig& config) const
{
    return config.matterEnabled;
}

esp_err_t MatterConnector::start(const UserConfig& config)
{
    if (!config.connectors.matterEnabled) {
        status_.state = ConnectorState::Disabled;
        return ESP_OK;
    }
    const esp_err_t err = MatterPlatform::start(runtime_);
    status_.state = err == ESP_OK ? ConnectorState::Running : ConnectorState::Degraded;
    status_.lastError = err;
    return err;
}

void MatterConnector::stop()
{
    MatterPlatform::stop();
    status_.state = ConnectorState::Stopped;
}

esp_err_t MatterConnector::reconfigure(const UserConfig& config)
{
    stop();
    return start(config);
}

void MatterConnector::tick()
{
    if (status_.state == ConnectorState::Running) {
        MatterPlatform::sync();
    }
}

ConnectorStatus MatterConnector::status() const
{
    return status_;
}

}  // namespace opencomfort
