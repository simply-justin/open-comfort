#include "src/Profiles/HttpDownloader.hpp"

#include <cstring>

extern "C" {
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
}

namespace opencomfort {
namespace {

constexpr const char* kTag = "HttpDownloader";

struct DownloadContext {
    std::string* body = nullptr;
    size_t maxBytes = 0;
    bool overflow = false;
};

esp_err_t onHttpEvent(esp_http_client_event_t* event)
{
    if (event == nullptr || event->user_data == nullptr) {
        return ESP_OK;
    }
    auto* ctx = static_cast<DownloadContext*>(event->user_data);
    if (event->event_id == HTTP_EVENT_ON_DATA && event->data_len > 0) {
        if (ctx->body->size() + static_cast<size_t>(event->data_len) > ctx->maxBytes) {
            ctx->overflow = true;
            return ESP_FAIL;
        }
        ctx->body->append(static_cast<const char*>(event->data), event->data_len);
    }
    return ESP_OK;
}

}  // namespace

esp_err_t HttpDownloader::getHttps(const char* url, std::string& outBody, size_t maxBytes)
{
    outBody.clear();
    if (url == nullptr || std::strncmp(url, "https://", 8) != 0 || maxBytes == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    DownloadContext ctx;
    ctx.body = &outBody;
    ctx.maxBytes = maxBytes;

    esp_http_client_config_t config = {};
    config.url = url;
    config.event_handler = onHttpEvent;
    config.user_data = &ctx;
    config.timeout_ms = 15000;
    config.buffer_size = 1024;
    config.crt_bundle_attach = esp_crt_bundle_attach;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == nullptr) {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = esp_http_client_perform(client);
    const int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (ctx.overflow) {
        outBody.clear();
        return ESP_ERR_INVALID_SIZE;
    }
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "HTTPS GET failed: %s", esp_err_to_name(err));
        outBody.clear();
        return err;
    }
    if (status < 200 || status >= 300) {
        ESP_LOGE(kTag, "HTTPS status %d", status);
        outBody.clear();
        return ESP_ERR_INVALID_RESPONSE;
    }
    return ESP_OK;
}

}  // namespace opencomfort
