#include "src/Http/WebAssets.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>

extern "C" {
#include "esp_app_desc.h"
}

extern const uint8_t index_html_start[] asm("_binary_index_html_start");
extern const uint8_t index_html_end[] asm("_binary_index_html_end");
extern const uint8_t runtime_html_start[] asm("_binary_runtime_html_start");
extern const uint8_t runtime_html_end[] asm("_binary_runtime_html_end");
extern const uint8_t app_css_start[] asm("_binary_app_css_start");
extern const uint8_t app_css_end[] asm("_binary_app_css_end");
extern const uint8_t app_js_start[] asm("_binary_app_js_start");
extern const uint8_t app_js_end[] asm("_binary_app_js_end");
extern const uint8_t runtime_js_start[] asm("_binary_runtime_js_start");
extern const uint8_t runtime_js_end[] asm("_binary_runtime_js_end");
extern const uint8_t geist_woff2_start[] asm("_binary_geist_woff2_start");
extern const uint8_t geist_woff2_end[] asm("_binary_geist_woff2_end");

namespace opencomfort {
namespace {

struct Asset {
    const uint8_t* start;
    const uint8_t* end;
    const char* type;
};

Asset lookup(WebAsset asset)
{
    switch (asset) {
        case WebAsset::SetupHtml: return {index_html_start, index_html_end, "text/html; charset=utf-8"};
        case WebAsset::RuntimeHtml: return {runtime_html_start, runtime_html_end, "text/html; charset=utf-8"};
        case WebAsset::AppCss: return {app_css_start, app_css_end, "text/css; charset=utf-8"};
        case WebAsset::SetupJs: return {app_js_start, app_js_end, "application/javascript; charset=utf-8"};
        case WebAsset::Font: return {geist_woff2_start, geist_woff2_end, "font/woff2"};
        case WebAsset::RuntimeJs:
        default: return {runtime_js_start, runtime_js_end, "application/javascript; charset=utf-8"};
    }
}

const char* buildEtag()
{
    static char etag[20] = {};
    if (etag[0] == '\0') {
        char sha[17] = {};
        esp_app_get_elf_sha256(sha, sizeof(sha));
        std::snprintf(etag, sizeof(etag), "\"%s\"", sha);
    }
    return etag;
}

}  // namespace

esp_err_t sendWebAsset(httpd_req_t* request, WebAsset asset)
{
    const char* etag = buildEtag();
    httpd_resp_set_hdr(request, "Cache-Control", "no-cache");
    httpd_resp_set_hdr(request, "ETag", etag);

    char ifNoneMatch[24] = {};
    if (httpd_req_get_hdr_value_str(request, "If-None-Match", ifNoneMatch, sizeof(ifNoneMatch)) ==
            ESP_OK &&
        std::strcmp(ifNoneMatch, etag) == 0) {
        httpd_resp_set_status(request, "304 Not Modified");
        return httpd_resp_send(request, nullptr, 0);
    }

    const Asset found = lookup(asset);
    httpd_resp_set_type(request, found.type);
    return httpd_resp_send(request, reinterpret_cast<const char*>(found.start),
                           static_cast<ssize_t>(found.end - found.start));
}

}  // namespace opencomfort
