#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>

#include "esp_err.h"

namespace opencomfort {

enum class UpdateState : uint8_t {
    Idle = 0,
    Checking,
    UpToDate,
    Available,
    Downloading,
    Restarting,
    Failed,
};

const char* toString(UpdateState state);

struct UpdateStatus {
    UpdateState state = UpdateState::Idle;
    char current[32] = {};
    char latest[32] = {};
    char variant[24] = {};
    char error[48] = {};
    uint8_t progress = 0;      // percent while downloading
    bool autoInstall = false;
    bool prerelease = false;   // channel: false = releases only, true = include pre-releases
    bool enabled = false;      // a release repository is configured
    int64_t lastCheckUs = 0;   // esp_timer time of the last finished check, 0 = never
};

// "v1.2.3" and "v1.2.3-beta.2" -> comparable number (alpha < beta < rc < release);
// -1 for anything else, including development builds such as "v1.2.3-4-gabc".
int64_t parseReleaseVersion(const char* version);
// Release asset name for this build, e.g. "opencomfort-matter-8mb.bin".
const char* updateVariant();

// Finds newer firmware in the GitHub releases of CONFIG_OPENCOMFORT_UPDATE_REPO and
// installs it through the inactive OTA slot. Network work runs in its own task.
class UpdateService {
public:
    static UpdateService& instance();

    void begin();                      // loads the auto-install choice
    void tick(bool networkUp);         // daily check, from the main loop
    esp_err_t checkNow();              // starts a check in the background
    esp_err_t installNow();            // downloads and installs the available version
    esp_err_t setAutoInstall(bool enabled);
    esp_err_t setPrerelease(bool enabled);
    UpdateStatus status();
    // A file upload holds the OTA slot too; false while a check or download runs.
    bool reserve();
    void release();

private:
    UpdateService() = default;
    static void checkTask(void* arg);
    static void installTask(void* arg);
    void finishCheck(UpdateState state, const char* latest, const char* url, const char* error);
    void fail(const char* error);

    std::mutex mutex_;
    UpdateStatus status_{};
    char assetUrl_[256] = {};
    bool busy_ = false;
    int64_t nextCheckUs_ = 0;
};

}  // namespace opencomfort
