#pragma once

#include "esp_http_server.h"
#include "freertos/FreeRTOS.h"

#include "src/Connectors/IConnector.hpp"
#include "src/Product/ProductDefinition.hpp"
#include "src/Runtime/RuntimeController.hpp"

namespace opencomfort {

class RuntimeHttpConnector final : public IConnector {
public:
    RuntimeHttpConnector(RuntimeController& runtime, const ProductDefinition& product);
    ~RuntimeHttpConnector() override;

    const char* id() const override;
    bool enabled(const ConnectorConfig& config) const override;
    esp_err_t start(const UserConfig& config) override;
    void stop() override;
    esp_err_t reconfigure(const UserConfig& config) override;
    void tick() override;
    ConnectorStatus status() const override;

    RuntimeController& runtime();
    const ProductDefinition& product() const;
    const UserConfig& config() const;
    void setConfig(const UserConfig& config);
    bool authenticate(const char* password);
    bool isAuthorized(httpd_req_t* request, bool requireCsrf) const;
    const char* sessionToken() const;
    void logout();
    void scheduleRestart();

private:
    bool startMdns();
    void stopMdns();

    RuntimeController& runtime_;
    const ProductDefinition& product_;
    UserConfig config_{};
    httpd_handle_t server_ = nullptr;
    ConnectorStatus status_{"web", ConnectorState::Stopped, ESP_OK, 0};
    char sessionToken_[33] = {};
    TickType_t sessionExpiresAt_ = 0;
    TickType_t nextLoginAllowedAt_ = 0;
    uint8_t failedLoginAttempts_ = 0;
    TickType_t restartAt_ = 0;
    bool mdnsStarted_ = false;
};

}  // namespace opencomfort
