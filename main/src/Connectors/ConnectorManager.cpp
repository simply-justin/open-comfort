#include "src/Connectors/ConnectorManager.hpp"

namespace opencomfort {

bool ConnectorManager::add(std::unique_ptr<IConnector> connector)
{
    if (!connector || count_ >= kMaxRuntimeConnectors) {
        return false;
    }
    connectors_[count_++] = std::move(connector);
    return true;
}

esp_err_t ConnectorManager::start(const UserConfig& config)
{
    config_ = config;
    esp_err_t firstError = ESP_OK;
    for (size_t i = 0; i < count_; ++i) {
        if (!connectors_[i]->enabled(config.connectors)) {
            connectors_[i]->stop();
            continue;
        }
        const esp_err_t err = connectors_[i]->start(config);
        if (err != ESP_OK && firstError == ESP_OK) {
            firstError = err;
        }
    }
    return firstError;
}

esp_err_t ConnectorManager::reconfigure(const UserConfig& config)
{
    config_ = config;
    esp_err_t firstError = ESP_OK;
    for (size_t i = 0; i < count_; ++i) {
        esp_err_t err = ESP_OK;
        if (connectors_[i]->enabled(config.connectors)) {
            err = connectors_[i]->reconfigure(config);
        } else {
            connectors_[i]->stop();
        }
        if (err != ESP_OK && firstError == ESP_OK) {
            firstError = err;
        }
    }
    return firstError;
}

void ConnectorManager::tick()
{
    for (size_t i = 0; i < count_; ++i) {
        connectors_[i]->tick();
    }
}

void ConnectorManager::stop()
{
    for (size_t i = 0; i < count_; ++i) {
        connectors_[i]->stop();
    }
}

size_t ConnectorManager::status(ConnectorStatus* output, size_t capacity) const
{
    if (output == nullptr) {
        return count_;
    }
    const size_t copied = count_ < capacity ? count_ : capacity;
    for (size_t i = 0; i < copied; ++i) {
        output[i] = connectors_[i]->status();
    }
    return copied;
}

}  // namespace opencomfort
