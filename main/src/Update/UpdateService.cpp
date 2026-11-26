#include "src/Product/ProductDefinition.hpp"
#include "src/Update/UpdateService.hpp"

#include <cctype>
#include <cstdio>
#include <cstring>
#include <string>

#include "sdkconfig.h"

#include "src/Profiles/HttpDownloader.hpp"

extern "C" {
#include "cJSON.h"
#include "esp_app_desc.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
}

namespace opencomfort {
namespace {

constexpr const char* kTag = "Update";
constexpr const char* kNamespace = "oc_user";  // cleared by a factory reset
constexpr const char* kAutoKey = "upd_auto";
constexpr const char* kChannelKey = "upd_pre";
constexpr int64_t kFirstCheckUs = 2LL * 60 * 1000000;          // 2 min after boot
constexpr int64_t kCheckEveryUs = 24LL * 60 * 60 * 1000000;    // daily
constexpr size_t kMaxReleaseJson = 48 * 1024;

void copy(char* out, size_t size, const char* text)
{
    std::snprintf(out, size, "%s", text != nullptr ? text : "");
}

}  // namespace

const char* toString(UpdateState state)
{
    switch (state) {
        case UpdateState::Checking: return "checking";
        case UpdateState::UpToDate: return "up_to_date";
        case UpdateState::Available: return "available";
        case UpdateState::Downloading: return "downloading";
        case UpdateState::Restarting: return "restarting";
        case UpdateState::Failed: return "failed";
        case UpdateState::Idle:
        default: return "idle";
    }
}

int64_t parseReleaseVersion(const char* version)
{
    if (version == nullptr) return -1;
    if (*version == 'v' || *version == 'V') ++version;
    unsigned major = 0, minor = 0, patch = 0;
    int consumed = 0;
    if (std::sscanf(version, "%u.%u.%u%n", &major, &minor, &patch, &consumed) != 3) return -1;
    if (major > 9999 || minor > 9999 || patch > 9999) return -1;
    const char* suffix = version + consumed;
    int64_t stage = 999;  // a final release sorts after its pre-releases
    if (*suffix != '\0') {
        // Only "-alpha[.N]", "-beta[.N]" and "-rc[.N]"; "-4-gabc" (git describe) is no release.
        static const struct { const char* name; int base; } kStages[] = {
            {"-alpha", 0}, {"-beta", 300}, {"-rc", 600}};
        stage = -1;
        for (const auto& kind : kStages) {
            const size_t length = std::strlen(kind.name);
            if (std::strncmp(suffix, kind.name, length) != 0) continue;
            const char* rest = suffix + length;
            unsigned number = 0;
            if (*rest == '\0') {
                stage = kind.base;
            } else if ((*rest == '.' || std::isdigit(static_cast<unsigned char>(*rest))) &&
                       std::sscanf(rest + (*rest == '.' ? 1 : 0), "%u%n", &number, &consumed) == 1 &&
                       rest[(*rest == '.' ? 1 : 0) + consumed] == '\0' && number < 299) {
                stage = kind.base + 1 + number;
            }
            break;
        }
        if (stage < 0) return -1;
    }
    return ((static_cast<int64_t>(major) * 10000 + minor) * 10000 + patch) * 1000 + stage;
}

// One image per flash layout; the MHI wiring is detected at boot.
const char* updateVariant()
{
#if CONFIG_OPENCOMFORT_PROFILE_FULL && CONFIG_ESPTOOLPY_FLASHSIZE_8MB
    return "full-8mb";
#elif CONFIG_OPENCOMFORT_PROFILE_FULL
    return "full-16mb";
#elif CONFIG_OPENCOMFORT_PROFILE_MATTER && CONFIG_ESPTOOLPY_FLASHSIZE_8MB
    return "matter-8mb";
#elif CONFIG_OPENCOMFORT_PROFILE_MATTER
    return "matter-16mb";
#else
    return "ha-4mb";
#endif
}

UpdateService& UpdateService::instance()
{
    static UpdateService service;
    return service;
}

void UpdateService::begin()
{
    std::lock_guard<std::mutex> lock(mutex_);
    copy(status_.current, sizeof(status_.current), esp_app_get_description()->version);
    copy(status_.variant, sizeof(status_.variant), updateVariant());
    status_.enabled = CONFIG_OPENCOMFORT_UPDATE_REPO[0] != '\0';
    nvs_handle_t handle = 0;
    if (nvs_open(kNamespace, NVS_READONLY, &handle) == ESP_OK) {
        uint8_t value = 0;
        if (nvs_get_u8(handle, kAutoKey, &value) == ESP_OK) status_.autoInstall = value != 0;
        if (nvs_get_u8(handle, kChannelKey, &value) == ESP_OK) status_.prerelease = value != 0;
        nvs_close(handle);
    }
    nextCheckUs_ = esp_timer_get_time() + kFirstCheckUs;
}

void UpdateService::tick(bool networkUp)
{
    bool startCheck = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!status_.enabled || busy_ || !networkUp || esp_timer_get_time() < nextCheckUs_) return;
        nextCheckUs_ = esp_timer_get_time() + kCheckEveryUs;
        startCheck = true;
    }
    if (startCheck) (void)checkNow();
}

esp_err_t UpdateService::checkNow()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!status_.enabled) return ESP_ERR_NOT_SUPPORTED;
        if (busy_) return ESP_ERR_INVALID_STATE;
        busy_ = true;
        status_.state = UpdateState::Checking;
        status_.error[0] = '\0';
    }
    if (xTaskCreate(checkTask, "upd_check", 6144, this, 3, nullptr) != pdPASS) {
        fail("out_of_memory");
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void UpdateService::checkTask(void* arg)
{
    auto* self = static_cast<UpdateService*>(arg);
    bool prerelease = false;
    {
        std::lock_guard<std::mutex> lock(self->mutex_);
        prerelease = self->status_.prerelease;
    }
    // "latest" never returns pre-releases; the list does, newest first.
    char url[160];
    std::snprintf(url, sizeof(url),
                  prerelease ? "https://api.github.com/repos/%s/releases?per_page=5"
                             : "https://api.github.com/repos/%s/releases/latest",
                  CONFIG_OPENCOMFORT_UPDATE_REPO);
    std::string body;
    const esp_err_t err = HttpDownloader::getHttps(url, body, kMaxReleaseJson);
    if (err != ESP_OK) {
        self->fail(err == ESP_ERR_INVALID_RESPONSE ? "no_release_found" : "network_error");
        vTaskDelete(nullptr);
        return;
    }
    cJSON* root = cJSON_Parse(body.c_str());
    body.clear();
    if (root == nullptr) {
        self->fail("bad_release_data");
        vTaskDelete(nullptr);
        return;
    }

    char wanted[48];
    std::snprintf(wanted, sizeof(wanted), "opencomfort-%s.bin", updateVariant());
    // The best release that has a build for this device.
    int64_t bestVersion = -1;
    const char* bestTag = nullptr;
    const char* bestUrl = nullptr;
    const char* anyTag = nullptr;
    auto consider = [&](const cJSON* release) {
        const cJSON* tag = cJSON_GetObjectItem(release, "tag_name");
        const cJSON* assets = cJSON_GetObjectItem(release, "assets");
        if (!cJSON_IsString(tag) || !cJSON_IsArray(assets) || cJSON_IsTrue(cJSON_GetObjectItem(release, "draft"))) return;
        if (!prerelease && cJSON_IsTrue(cJSON_GetObjectItem(release, "prerelease"))) return;
        if (anyTag == nullptr) anyTag = tag->valuestring;
        const int64_t version = parseReleaseVersion(tag->valuestring);
        if (version <= bestVersion) return;
        const cJSON* asset = nullptr;
        cJSON_ArrayForEach(asset, assets)
        {
            const cJSON* name = cJSON_GetObjectItem(asset, "name");
            const cJSON* link = cJSON_GetObjectItem(asset, "browser_download_url");
            if (cJSON_IsString(name) && cJSON_IsString(link) && std::strcmp(name->valuestring, wanted) == 0 &&
                std::strncmp(link->valuestring, "https://", 8) == 0) {
                bestVersion = version;
                bestTag = tag->valuestring;
                bestUrl = link->valuestring;
                return;
            }
        }
    };
    if (cJSON_IsArray(root)) {
        const cJSON* release = nullptr;
        cJSON_ArrayForEach(release, root) consider(release);
    } else {
        consider(root);
    }

    const int64_t current = parseReleaseVersion(esp_app_get_description()->version);
    if (bestUrl == nullptr) {
        self->finishCheck(UpdateState::Failed, anyTag, nullptr,
                          anyTag == nullptr ? "no_release_found" : "no_build_for_this_device");
    } else if (current >= 0 && bestVersion <= current) {
        self->finishCheck(UpdateState::UpToDate, bestTag, nullptr, nullptr);
    } else {
        // Development builds (no release version) are offered the newest release too.
        self->finishCheck(UpdateState::Available, bestTag, bestUrl, nullptr);
    }
    cJSON_Delete(root);
    vTaskDelete(nullptr);
}

void UpdateService::finishCheck(UpdateState state, const char* latest, const char* url, const char* error)
{
    bool autoInstall = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        status_.state = state;
        copy(status_.latest, sizeof(status_.latest), latest);
        copy(status_.error, sizeof(status_.error), error);
        copy(assetUrl_, sizeof(assetUrl_), url);
        status_.lastCheckUs = esp_timer_get_time();
        busy_ = false;
        // Automatic installs only move between real releases, never over a development build.
        autoInstall = state == UpdateState::Available && status_.autoInstall &&
                      parseReleaseVersion(status_.current) >= 0;
    }
    ESP_LOGI(kTag, "Check: %s (latest %s)", toString(state), latest != nullptr ? latest : "-");
    if (autoInstall) (void)installNow();
}

void UpdateService::fail(const char* error)
{
    std::lock_guard<std::mutex> lock(mutex_);
    status_.state = UpdateState::Failed;
    copy(status_.error, sizeof(status_.error), error);
    status_.lastCheckUs = esp_timer_get_time();
    busy_ = false;
    ESP_LOGW(kTag, "Update failed: %s", error);
}

bool UpdateService::reserve()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (busy_) return false;
    busy_ = true;
    return true;
}

void UpdateService::release()
{
    std::lock_guard<std::mutex> lock(mutex_);
    busy_ = false;
}

esp_err_t UpdateService::installNow()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (busy_) return ESP_ERR_INVALID_STATE;
        if (status_.state != UpdateState::Available || assetUrl_[0] == '\0') return ESP_ERR_NOT_FOUND;
        busy_ = true;
        status_.state = UpdateState::Downloading;
        status_.progress = 0;
        status_.error[0] = '\0';
    }
    if (xTaskCreate(installTask, "upd_install", 8192, this, 3, nullptr) != pdPASS) {
        fail("out_of_memory");
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void UpdateService::installTask(void* arg)
{
    auto* self = static_cast<UpdateService*>(arg);
    char url[sizeof(self->assetUrl_)];
    {
        std::lock_guard<std::mutex> lock(self->mutex_);
        copy(url, sizeof(url), self->assetUrl_);
    }

    esp_http_client_config_t http = {};
    http.url = url;
    http.crt_bundle_attach = esp_crt_bundle_attach;  // GitHub redirects to its CDN, also verified
    http.timeout_ms = 20000;
    http.buffer_size = 2048;
    http.buffer_size_tx = 1024;
    http.keep_alive_enable = true;
    esp_https_ota_config_t ota = {};
    ota.http_config = &http;

    esp_https_ota_handle_t handle = nullptr;
    if (esp_https_ota_begin(&ota, &handle) != ESP_OK) {
        self->fail("download_failed");
        vTaskDelete(nullptr);
        return;
    }
    esp_app_desc_t desc{};
    if (esp_https_ota_get_img_desc(handle, &desc) != ESP_OK ||
        std::strncmp(desc.project_name, esp_app_get_description()->project_name,
                     sizeof(desc.project_name)) != 0) {
        esp_https_ota_abort(handle);
        self->fail("not_opencomfort_firmware");
        vTaskDelete(nullptr);
        return;
    }

    esp_err_t err = ESP_ERR_HTTPS_OTA_IN_PROGRESS;
    const int total = esp_https_ota_get_image_size(handle);
    while (err == ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
        err = esp_https_ota_perform(handle);
        const int read = esp_https_ota_get_image_len_read(handle);
        if (total > 0) {
            std::lock_guard<std::mutex> lock(self->mutex_);
            self->status_.progress = static_cast<uint8_t>((static_cast<int64_t>(read) * 100) / total);
        }
    }
    if (err != ESP_OK || !esp_https_ota_is_complete_data_received(handle)) {
        esp_https_ota_abort(handle);
        self->fail("download_interrupted");
        vTaskDelete(nullptr);
        return;
    }
    // Verifies the image (and its signature when Secure Boot is on) and selects it for boot.
    if (esp_https_ota_finish(handle) != ESP_OK) {
        self->fail("firmware_invalid");
        vTaskDelete(nullptr);
        return;
    }
    {
        std::lock_guard<std::mutex> lock(self->mutex_);
        self->status_.state = UpdateState::Restarting;
        self->status_.progress = 100;
    }
    ESP_LOGI(kTag, "Update installed, restarting");
    vTaskDelay(pdMS_TO_TICKS(1500));  // lets the page see "restarting"
    esp_restart();
}

esp_err_t UpdateService::setAutoInstall(bool enabled)
{
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(kNamespace, NVS_READWRITE, &handle);
    if (err == ESP_OK) {
        err = nvs_set_u8(handle, kAutoKey, enabled ? 1 : 0);
        if (err == ESP_OK) err = nvs_commit(handle);
        nvs_close(handle);
    }
    if (err == ESP_OK) {
        std::lock_guard<std::mutex> lock(mutex_);
        status_.autoInstall = enabled;
    }
    return err;
}

esp_err_t UpdateService::setPrerelease(bool enabled)
{
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(kNamespace, NVS_READWRITE, &handle);
    if (err == ESP_OK) {
        err = nvs_set_u8(handle, kChannelKey, enabled ? 1 : 0);
        if (err == ESP_OK) err = nvs_commit(handle);
        nvs_close(handle);
    }
    if (err == ESP_OK) {
        std::lock_guard<std::mutex> lock(mutex_);
        status_.prerelease = enabled;
        // A result from the other channel no longer applies.
        if (!busy_) {
            status_.state = UpdateState::Idle;
            status_.latest[0] = '\0';
            assetUrl_[0] = '\0';
        }
    }
    return err;
}

UpdateStatus UpdateService::status()
{
    std::lock_guard<std::mutex> lock(mutex_);
    return status_;
}

}  // namespace opencomfort
