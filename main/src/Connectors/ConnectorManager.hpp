#pragma once

#include <cstddef>
#include <memory>

#include "src/Connectors/IConnector.hpp"

namespace opencomfort {

constexpr size_t kMaxRuntimeConnectors = 3;

class ConnectorManager {
public:
    bool add(std::unique_ptr<IConnector> connector);
    esp_err_t start(const UserConfig& config);
    esp_err_t reconfigure(const UserConfig& config);
    void tick();
    void stop();
    size_t status(ConnectorStatus* output, size_t capacity) const;

private:
    std::unique_ptr<IConnector> connectors_[kMaxRuntimeConnectors];
    size_t count_ = 0;
    UserConfig config_{};
};

}  // namespace opencomfort
