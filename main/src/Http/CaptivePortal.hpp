#pragma once

#include "src/Configs/DeviceConfig.hpp"

extern "C" {
    #include "esp_http_server.h"
}

/**
 * @brief First-boot setup portal that creates a DeviceConfig.
 */
class CaptivePortal {
public:
    CaptivePortal() = default;
    DeviceConfig run();

private:
    DeviceConfig result_;
    httpd_handle_t server_ = nullptr;

    bool finished_ = false;

    void startSoftAp();
    void startHttpServer();
    void stopHttpServer();

    static esp_err_t rootHandler(httpd_req_t* req);
    static esp_err_t productsHandler(httpd_req_t* req);
    static esp_err_t setupHandler(httpd_req_t* req);
};
