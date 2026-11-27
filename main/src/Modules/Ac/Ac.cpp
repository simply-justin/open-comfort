#include "src/Modules/Ac/Ac.hpp"

#include <cmath>
#include <cstring>
#include <cstring>
#include "src/Modules/Ac/MhiLink.hpp"
#include <string>

#include "src/Config/UserConfigStore.hpp"
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

    mhiLink_ = profile_.bus == BusKind::Spi &&
               std::strcmp(profile_.profileId, "mitsubishi_heavy_industries") == 0;
    if (mhiLink_) {
        const MhiWiring& wiring =
            product_.board->mhiWirings[mhi::detectWiring(product_.board->mhiWirings, kMhiWiringCount)];
        const esp_err_t err = mhi::start(wiring.pins, wiring.extendedFrames);
        if (err != ESP_OK) {
            ESP_LOGE(kTag, "MHI SPI peripheral initialization failed: %s", esp_err_to_name(err));
            return;
        }
        bus_.kind = BusKind::Spi;
    } else {
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
    }

    // Restore the last applied state without transmitting it: a restart (settings change,
    // update, power blip) must not switch the unit off or change what it is doing.
    desired_ = AcDesiredState{};
    if (UserConfigStore::loadAcDesired(desired_) && !isValidDesired(desired_)) {
        desired_ = AcDesiredState{};
    }

    config_.ac.bus = bus_.kind;
    nextPoll_ = xTaskGetTickCount();
    booted_ = true;
    ESP_LOGI(kTag, "AC module booted on bus=%s", toString(bus_.kind));
}

bool AcModule::isValidDesired(const AcDesiredState& desired) const
{
    const AcProtocolProfile& capabilities = protocol_.profile();
    return std::isfinite(desired.targetTempC) &&
           desired.targetTempC >= static_cast<float>(capabilities.minTempC) &&
           desired.targetTempC <= static_cast<float>(capabilities.maxTempC) &&
           static_cast<uint8_t>(desired.mode) <= static_cast<uint8_t>(AcMode::Auto) &&
           static_cast<uint8_t>(desired.fan) <= static_cast<uint8_t>(AcFanSpeed::High) &&
           (capabilities.vaneCount == 0 ? desired.vanes == 0 : desired.vanes < capabilities.vaneCount);
}

bool AcModule::isBooted() const
{
    return booted_;
}

esp_err_t AcModule::applyRuntimeDesired(const AcDesiredState& desired)
{
    if (!booted_ || !protocol_.isLoaded()) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!isValidDesired(desired)) {
        return ESP_ERR_INVALID_ARG;
    }

    AcReportedState latest = reported_;
    const esp_err_t err = mhiLink_ ? mhi::exchange(&desired, latest)
                                   : protocol_.applyDesiredState(bus_, desired);
    if (mhiLink_) applyReading(err == ESP_OK && latest.valid, latest);
    if (err == ESP_OK) {
        const bool changed = desired.powerOn != desired_.powerOn || desired.mode != desired_.mode ||
                             desired.targetTempC != desired_.targetTempC || desired.fan != desired_.fan ||
                             desired.vanes != desired_.vanes;
        desired_ = desired;
        if (changed && UserConfigStore::saveAcDesired(desired_) != ConfigStatus::Ok) {
            ESP_LOGW(kTag, "Could not persist AC state");
        }
    }
    return err;
}

RuntimeSnapshot AcModule::runtimeSnapshot() const
{
    RuntimeSnapshot snapshot;
    snapshot.module = ModuleKind::Ac;
    snapshot.available = booted_;
    snapshot.desired = desired_;
    snapshot.reported = reported_;
    if (protocol_.isLoaded()) {
        const AcProtocolProfile& profile = protocol_.profile();
        snapshot.acCapabilities.minTempC = profile.minTempC;
        snapshot.acCapabilities.maxTempC = profile.maxTempC;
        snapshot.acCapabilities.vaneCount = profile.vaneCount;
        std::memcpy(snapshot.acCapabilities.vaneNames, profile.vaneNames, sizeof(profile.vaneNames));
        for (size_t i = 0; i < profile.dataRequestCount; ++i) {
            if (profile.dataRequests[i].report == AcReport::Compressor) {
                snapshot.acCapabilities.reportsAction = true;
            }
        }
    }
    return snapshot;
}

esp_err_t AcModule::setDesiredState(const AcDesiredState& desired)
{
    return applyRuntimeDesired(desired);
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
    if (static_cast<int32_t>(now - nextPoll_) < 0) {  // wrap-safe: ticks wrap after 49.7 days
        vTaskDelay(pdMS_TO_TICKS(20));
        return;
    }
    nextPoll_ = now + (mhiLink_ ? pdMS_TO_TICKS(100) : kPollInterval);

    AcReportedState latest;
    const esp_err_t pollError = mhiLink_ ? mhi::exchange(nullptr, latest) : protocol_.poll(bus_, latest);
    applyReading(pollError == ESP_OK && latest.valid, latest);
}

// A single lost frame (a stray clock edge) must not blank the state: Home Assistant and the
// portal would flash "not answering". Only after 5 s without a valid frame is it cleared.
void AcModule::applyReading(bool ok, AcReportedState latest)
{
    const TickType_t now = xTaskGetTickCount();
    if (!ok) {
        if (now - lastValid_ >= pdMS_TO_TICKS(5000)) reported_ = {};
        return;
    }
    lastValid_ = now;
    holdRoomTemp(latest);
    reported_ = latest;
}

// The unit reports room temperature in 0.25 °C steps that flicker between two neighbours;
// Home Assistant shows whole degrees, so near x.5 it jumped a degree on every poll. Only
// follow a change of 0.5 °C or more.
void AcModule::holdRoomTemp(AcReportedState& state)
{
    if (!state.hasRoomTemp) return;
    if (std::isnan(shownRoomC_) || std::fabs(state.returnAirC - shownRoomC_) >= 0.5f) shownRoomC_ = state.returnAirC;
    state.returnAirC = shownRoomC_;
}

}  // namespace opencomfort
