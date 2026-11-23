#pragma once

#include <string>

extern "C" {
    #include "esp_http_server.h"
}

namespace HttpBody {

/**
 * Reads the full request body (`req->content_len` bytes) into `body`.
 *
 * A single `httpd_req_recv` call is not guaranteed to return the whole
 * body — it can return fewer bytes than requested for any body that
 * doesn't fit in one TCP read, which a caller that reads once and trusts
 * the result would silently treat as the complete (truncated) body. This
 * loops until every byte has arrived, retrying on `HTTPD_SOCK_ERR_TIMEOUT`
 * (a transient "no data yet" condition, not a lost connection) the way
 * ESP-IDF's own httpd examples do, and fails on anything else.
 *
 * Returns false (and clears `body`) if `content_len` is outside
 * `(0, maxBytes]`, or if the socket errors out before the full body
 * arrives.
 */
inline bool read(httpd_req_t* req, std::string& body, int maxBytes) {
    const int len = req->content_len;
    if (len <= 0 || len > maxBytes) {
        body.clear();
        return false;
    }

    body.resize(static_cast<size_t>(len));

    size_t received = 0;
    int timeoutRetries = 0;
    constexpr int kMaxTimeoutRetries = 50;

    while (received < body.size()) {
        const int chunk = httpd_req_recv(req, body.data() + received, body.size() - received);
        if (chunk == HTTPD_SOCK_ERR_TIMEOUT) {
            if (++timeoutRetries > kMaxTimeoutRetries) {
                body.clear();
                return false;
            }
            continue;
        }
        if (chunk <= 0) {
            body.clear();
            return false;
        }
        received += static_cast<size_t>(chunk);
    }

    return true;
}

} // namespace HttpBody
