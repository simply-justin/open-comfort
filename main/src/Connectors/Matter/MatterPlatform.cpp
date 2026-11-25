#include "src/Connectors/Matter/MatterPlatform.hpp"

#include "sdkconfig.h"

namespace opencomfort {

#if !CONFIG_OPENCOMFORT_ENABLE_MATTER

esp_err_t MatterPlatform::start(RuntimeController&)
{
    return ESP_ERR_NOT_SUPPORTED;
}

void MatterPlatform::stop() {}

void MatterPlatform::sync() {}

esp_err_t MatterPlatform::openCommissioningWindow()
{
    return ESP_ERR_NOT_SUPPORTED;
}

bool MatterPlatform::commissioningWindowOpen()
{
    return false;
}

esp_err_t MatterPlatform::pairingCodes(char*, size_t, char*, size_t)
{
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t MatterPlatform::factoryReset()
{
    return ESP_OK;
}

bool MatterPlatform::isRunning()
{
    return false;
}

#endif

}  // namespace opencomfort
