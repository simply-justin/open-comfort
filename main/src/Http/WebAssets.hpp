#pragma once

#include "esp_http_server.h"

namespace opencomfort {

// Portal files are embedded in the app image (not SPIFFS), so a firmware update ships the
// matching UI with it.
enum class WebAsset {
    SetupHtml,
    RuntimeHtml,
    AppCss,
    SetupJs,
    RuntimeJs,
    Font,  // Geist (SIL OFL 1.1, see main/www/fonts/OFL.txt)
};

// Sends the asset with an ETag tied to this firmware build; a matching If-None-Match gets 304.
esp_err_t sendWebAsset(httpd_req_t* request, WebAsset asset);

}  // namespace opencomfort
