#include "src/Profiles/HttpDownloader.hpp"

#include <cstring>

extern "C" {
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
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
    bool noMemory = false;
};

// Grows the body without risking abort(): this build has no exceptions, so a failed
// std::string allocation would panic the device. Capacity is reserved only when the heap
// has a free block that large (plus room for Wi-Fi and TLS), else the download stops.
bool reserveFor(DownloadContext& ctx, size_t needed)
{
    std::string& body = *ctx.body;
    if (needed <= body.capacity()) return true;
    size_t grown = body.capacity() < 1024 ? 1024 : body.capacity() * 2;
    if (grown < needed) grown = needed;
    if (grown > ctx.maxBytes) grown = ctx.maxBytes;
    constexpr size_t kHeadroom = 16 * 1024;
    const size_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    if (largest < grown + kHeadroom) {
        // Measured here, while the TLS session still holds its buffers.
        ESP_LOGW(kTag, "Download stopped at %u bytes: needs a %u byte block, largest free is %u",
                 unsigned(body.size()), unsigned(grown + kHeadroom), unsigned(largest));
        return false;
    }
    body.reserve(grown);
    return true;
}

esp_err_t onHttpEvent(esp_http_client_event_t* event)
{
    if (event == nullptr || event->user_data == nullptr) {
        return ESP_OK;
    }
    auto* ctx = static_cast<DownloadContext*>(event->user_data);
    if (event->event_id == HTTP_EVENT_ON_DATA && event->data_len > 0) {
        const size_t needed = ctx->body->size() + static_cast<size_t>(event->data_len);
        if (needed > ctx->maxBytes) {
            ctx->overflow = true;
            return ESP_FAIL;
        }
        if (!reserveFor(*ctx, needed)) {
            ctx->noMemory = true;
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

    if (ctx.overflow || ctx.noMemory) {
        std::string().swap(outBody);
        return ctx.overflow ? ESP_ERR_INVALID_SIZE : ESP_ERR_NO_MEM;
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
