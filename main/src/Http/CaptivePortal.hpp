#pragma once

#include "src/Models/UserConfig.hpp"
#include "src/Board/BoardProfile.hpp"

/**
 * @brief Captive portal that creates a UserConfig.
 *
 * Steps:
 *  1) WiFi credentials
 *  2) Product/module selection (filtered by BoardProfile)
 *  3) Module-specific setup (dynamic)
 *  4) Save config (NVS "user") and return
 */
class CaptivePortal {
public:
    explicit CaptivePortal(const BoardProfile& boardProfile);

    UserConfig run();

private:
    BoardProfile boardProfile_;
    UserConfig working_;

    bool finished_ = false;

    void startSoftAp();
    void stopSoftAp();

    void startHttpServer();
    void stopHttpServer();
};
