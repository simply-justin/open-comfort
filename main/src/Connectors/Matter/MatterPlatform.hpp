#pragma once

#include <cstddef>

#include "esp_err.h"

namespace opencomfort {

class RuntimeController;

class MatterPlatform {
public:
    static esp_err_t start(RuntimeController& runtime);
    static void stop();
    static void sync();
    static esp_err_t openCommissioningWindow();
    static bool commissioningWindowOpen();
    // Onboarding payload ("MT:...") and the 11-digit manual pairing code.
    static esp_err_t pairingCodes(char* qrPayload, size_t qrLength, char* manualCode,
                                  size_t manualLength);
    static esp_err_t factoryReset();
    static bool isRunning();
};

}  // namespace opencomfort
