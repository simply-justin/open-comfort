#include "src/Modules/Ac/Ac.hpp"

#include <string>

#include "src/Profiles/ProfileRepository.hpp"

extern "C" {
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
}

namespace opencomfort {
namespace {
constexpr const char* kTag = "AcModule";
constexpr TickType_t kPollInterval = pdMS_TO_TICKS(2000);
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

    std::string profileJson;
    if (ProfileRepository::loadProfileJson(config_.ac.profileId, profileJson) != ESP_OK) {
        ESP_LOGE(kTag, "Failed to load AC profile JSON");
        return;
    }
    if (protocol_.loadFromJson(profileJson.c_str()) != ESP_OK) {
        ESP_LOGE(kTag, "Failed to parse AC protocol profile");
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

    // Push initial desired defaults once so the bus path is exercised safely.
    desired_ = AcDesiredState{};
    desired_.powerOn = false;
    desired_.mode = AcMode::Cool;
    desired_.targetTempC = 22.0f;
    desired_.fan = AcFanSpeed::Auto;
    if (protocol_.applyDesiredState(bus_, desired_) != ESP_OK) {
        ESP_LOGW(kTag, "Initial desired-state transmit failed (bus may be disconnected)");
    }

    config_.ac.bus = bus_.kind;
    nextPoll_ = xTaskGetTickCount();
    booted_ = true;
    ESP_LOGI(kTag, "AC module booted on bus=%s", toString(bus_.kind));
}

bool AcModule::isBooted() const
{
    return booted_;
}

esp_err_t AcModule::setDesiredState(const AcDesiredState& desired)
{
    if (!booted_) {
        return ESP_ERR_INVALID_STATE;
    }
    desired_ = desired;
    return protocol_.applyDesiredState(bus_, desired_);
}

AcDesiredState AcModule::desiredState() const
{
    return desired_;
}

AcReportedState AcModule::reportedState() const
{
    return reported_;
}

void AcModule::run()
{
    if (!booted_) {
        return;
    }

    const TickType_t now = xTaskGetTickCount();
    if (now < nextPoll_) {
        vTaskDelay(pdMS_TO_TICKS(20));
        return;
    }
    nextPoll_ = now + kPollInterval;

    AcReportedState latest;
    if (protocol_.poll(bus_, latest) == ESP_OK && latest.valid) {
        reported_ = latest;
        ESP_LOGD(kTag, "Poll returnAir=%.1f setTemp=%.1f", reported_.returnAirC,
                 reported_.setTempC);
    }
}

}  // namespace opencomfort
