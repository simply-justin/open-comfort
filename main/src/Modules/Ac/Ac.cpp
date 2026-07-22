#include "src/Modules/Ac/Ac.hpp"

#include "src/Profiles/ProfileRepository.hpp"

extern "C" {
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
}

namespace opencomfort {
namespace {
constexpr const char* kTag = "AcModule";
}

AcModule::AcModule(const ProductDefinition& product, UserConfig config)
    : product_(product), config_(std::move(config))
{
}

void AcModule::bootstrap()
{
    booted_ = false;
    ESP_LOGI(kTag, "Bootstrapping AC module profile=%s model=%s", config_.ac.profileId,
             config_.ac.modelId);

    if (ProfileRepository::ensureProfileAvailable(product_, config_.ac.profileId, profile_) !=
        ESP_OK) {
        ESP_LOGE(kTag, "Failed to load AC profile");
        return;
    }

    ProtocolBusParams protocol;
    protocol.kind = profile_.bus;
    protocol.baudOrClockHz = profile_.baudOrClockHz;
    protocol.spiMode = profile_.spiMode;

    BusConfigVariant busCfg;
    const BusStatus mergeStatus = BusFactory::merge(product_, protocol, busCfg);
    if (mergeStatus != BusStatus::Ok) {
        ESP_LOGE(kTag, "Bus merge failed: %s", toString(mergeStatus));
        return;
    }

    const BusStatus validateStatus = BusFactory::validateConfig(busCfg);
    if (validateStatus != BusStatus::Ok) {
        ESP_LOGE(kTag, "Bus validation failed: %s", toString(validateStatus));
        return;
    }

    const BusResult opened = BusFactory::createOpen(busCfg, bus_);
    if (!opened) {
        ESP_LOGE(kTag, "Bus open failed: %s", toString(opened.status));
        return;
    }

    config_.ac.bus = bus_.kind;
    booted_ = true;
    ESP_LOGI(kTag, "AC module booted on bus=%s", toString(bus_.kind));
}

bool AcModule::isBooted() const
{
    return booted_;
}

void AcModule::run()
{
    if (!booted_) {
        return;
    }

    // MVP: keep the transport alive and yield. Protocol frame engine follows later.
    vTaskDelay(pdMS_TO_TICKS(50));
}

}  // namespace opencomfort
