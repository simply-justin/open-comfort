#pragma once

#include "esp_http_server.h"
#include "freertos/FreeRTOS.h"

#include "src/Connectors/ConnectorManager.hpp"
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
    struct Session {
        char token[33] = {};  // cookie value (HttpOnly)
        char csrf[33] = {};   // returned to the page, sent back in X-OC-CSRF
        TickType_t expiresAt = 0;
    };

    // Returns the new session, or nullptr when the password is wrong or login is throttled.
    const Session* authenticate(const char* password);
    bool isAuthorized(httpd_req_t* request, bool requireCsrf) const;
    void logout(httpd_req_t* request);
    // The 32-character oc_session value from a Cookie header, or nullptr. Exposed for tests.
    static const char* sessionCookieValue(const char* cookieHeader);
    void scheduleRestart();
    // For /status; the manager outlives this connector.
    void setConnectorManager(const ConnectorManager* manager);
    const ConnectorManager* connectorManager() const;
    // Room temperature once a minute for 24 h, oldest first; INT16_MIN = no reading.
    static constexpr size_t kHistoryLength = 1440;
    size_t copyHistory(int16_t* out, size_t capacity) const;
    void markCommissioningOpened();
    uint32_t commissioningSecondsLeft() const;

private:
    bool startMdns();
    bool startSharedMdns();
    void stopMdns();

    RuntimeController& runtime_;
    const ProductDefinition& product_;
    UserConfig config_{};
    httpd_handle_t server_ = nullptr;
    ConnectorStatus status_{"web", ConnectorState::Stopped, ESP_OK, 0};
    Session* findSession(httpd_req_t* request) const;

    // A few concurrent sign-ins (phone, laptop, tablet); the oldest is replaced when full.
    static constexpr size_t kMaxSessions = 4;
    mutable Session sessions_[kMaxSessions] = {};
    TickType_t nextLoginAllowedAt_ = 0;
    uint8_t failedLoginAttempts_ = 0;
    TickType_t restartAt_ = 0;
    const ConnectorManager* connectorManager_ = nullptr;
    // Written by tick() on the main loop, read by the HTTP task; a torn read only
    // misplaces one sample in the chart, so no lock.
    int16_t history_[kHistoryLength] = {};
    size_t historyHead_ = 0;
    size_t historyCount_ = 0;
    TickType_t nextSampleAt_ = 0;
    TickType_t commissioningOpenedAt_ = 0;
    bool mdnsStarted_ = false;
    char mdnsHostname_[kMaxHostnameLen + 1] = {};
    char mdnsInstance_[kMaxFriendlyNameLen + 1] = {};
    // With Matter, ESP-Matter owns the mDNS stack; this connector then adds its
    // hostname as a delegate once that stack is up.
    bool mdnsShared_ = false;
    TickType_t mdnsDeferredSince_ = 0;
    TickType_t mdnsRetryAt_ = 0;
};

}  // namespace opencomfort
