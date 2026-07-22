#pragma once

#include "src/Connectors/IConnector.hpp"
#include "src/Runtime/RuntimeController.hpp"

namespace opencomfort {

class MatterConnector final : public IConnector {
public:
    explicit MatterConnector(RuntimeController& runtime);
    ~MatterConnector() override;

    const char* id() const override;
    bool enabled(const ConnectorConfig& config) const override;
    esp_err_t start(const UserConfig& config) override;
    void stop() override;
    esp_err_t reconfigure(const UserConfig& config) override;
    void tick() override;
    ConnectorStatus status() const override;

private:
    RuntimeController& runtime_;
    ConnectorStatus status_{"matter", ConnectorState::Stopped, ESP_OK, 0};
};

}  // namespace opencomfort
