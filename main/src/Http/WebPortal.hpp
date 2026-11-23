#pragma once

#include "src/Configs/DeviceConfig.hpp"
#include "src/Modules/IModule.hpp"

extern "C" {
    #include "esp_http_server.h"
}

class WebPortal {
public:
    WebPortal(const DeviceConfig& config, IModule* module);
    bool start();
    void stop();

private:
    DeviceConfig config_;
    IModule* module_;
    httpd_handle_t server_ = nullptr;

    static esp_err_t rootHandler(httpd_req_t* req);
    static esp_err_t statusHandler(httpd_req_t* req);
    static esp_err_t modulesHandler(httpd_req_t* req);
    static esp_err_t acHandler(httpd_req_t* req);
    static esp_err_t acStateHandler(httpd_req_t* req);
    static esp_err_t acCommandHandler(httpd_req_t* req);
    static esp_err_t setupResetHandler(httpd_req_t* req);
};
