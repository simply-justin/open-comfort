#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "esp_err.h"

namespace opencomfort {

class HttpDownloader {
public:
    // Downloads HTTPS body with certificate bundle verification.
    // Rejects responses larger than maxBytes.
    static esp_err_t getHttps(const char* url, std::string& outBody, size_t maxBytes);
};

}  // namespace opencomfort
