#pragma once

#include "esp_err.h"

#include "src/Config/UserConfig.hpp"

namespace opencomfort {

enum class ConnectorState : uint8_t {
    Disabled = 0,
    Stopped,
    Starting,
    Running,
    Degraded,
};

struct ConnectorStatus {
    const char* id = "";
    ConnectorState state = ConnectorState::Stopped;
    esp_err_t lastError = ESP_OK;
    uint32_t reconnectCount = 0;
};

class IConnector {
public:
    virtual ~IConnector() = default;
    virtual const char* id() const = 0;
    virtual bool enabled(const ConnectorConfig& config) const = 0;
    virtual esp_err_t start(const UserConfig& config) = 0;
    virtual void stop() = 0;
    virtual esp_err_t reconfigure(const UserConfig& config) = 0;
    virtual void tick() = 0;
    virtual ConnectorStatus status() const = 0;
};

}  // namespace opencomfort
