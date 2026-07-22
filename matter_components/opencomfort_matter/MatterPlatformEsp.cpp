#include "src/Connectors/Matter/MatterPlatform.hpp"

#include "src/Runtime/RuntimeController.hpp"

#include "sdkconfig.h"

#include <app/server/Server.h>
#include <app-common/zap-generated/ids/Attributes.h>
#include <app-common/zap-generated/ids/Clusters.h>
#include <esp_log.h>
#include <esp_matter.h>

namespace opencomfort {
namespace {

constexpr const char* kTag = "MatterPlatform";
constexpr chip::System::Clock::Seconds16 kCommissioningWindow(300);

RuntimeController* s_runtime = nullptr;
uint16_t s_endpointId = 0;
uint32_t s_publishedRevision = 0;
bool s_running = false;

using namespace chip::app::Clusters;
using namespace esp_matter;

uint8_t toMatterMode(const AcDesiredState& desired)
{
    if (!desired.powerOn || desired.mode == AcMode::Off) return 0;
    switch (desired.mode) {
        case AcMode::Auto: return 1;
        case AcMode::Cool: return 3;
        case AcMode::Heat: return 4;
        case AcMode::Fan: return 7;
        case AcMode::Dry: return 8;
        default: return 0;
    }
}

bool fromMatterMode(uint8_t value, AcDesiredState& desired)
{
    desired.powerOn = value != 0;
    switch (value) {
        case 0: desired.mode = AcMode::Off; return true;
        case 1: desired.mode = AcMode::Auto; return true;
        case 3: desired.mode = AcMode::Cool; return true;
        case 4: desired.mode = AcMode::Heat; return true;
        case 7: desired.mode = AcMode::Fan; return true;
        case 8: desired.mode = AcMode::Dry; return true;
        default: return false;
    }
}

esp_err_t attributeUpdate(attribute::callback_type_t type, uint16_t endpointId, uint32_t clusterId,
                          uint32_t attributeId, esp_matter_attr_val_t* value, void*)
{
    if (type != PRE_UPDATE || endpointId != s_endpointId || s_runtime == nullptr ||
        clusterId != Thermostat::Id) {
        return ESP_OK;
    }

    AcDesiredState desired = s_runtime->snapshot().desired;
    bool recognized = true;
    if (attributeId == Thermostat::Attributes::SystemMode::Id) {
        recognized = fromMatterMode(value->val.u8, desired);
    } else if (attributeId == Thermostat::Attributes::OccupiedCoolingSetpoint::Id ||
               attributeId == Thermostat::Attributes::OccupiedHeatingSetpoint::Id) {
        desired.targetTempC = static_cast<float>(value->val.i16) / 100.0f;
    } else {
        return ESP_OK;
    }
    if (!recognized) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    const RuntimeRequestStatus status =
        s_runtime->requestDesiredState(desired, RuntimeCommandSource::Matter);
    return status == RuntimeRequestStatus::Accepted ? ESP_OK : ESP_ERR_INVALID_STATE;
}

esp_err_t identify(identification::callback_type_t, uint16_t, uint8_t, uint8_t, void*)
{
    return ESP_OK;
}

void deviceEvent(const ChipDeviceEvent* event, intptr_t)
{
    if (event->Type == chip::DeviceLayer::DeviceEventType::kCommissioningComplete) {
        ESP_LOGI(kTag, "Commissioning complete");
    }
}

void updateAttribute(uint32_t attributeId, esp_matter_attr_val_t value)
{
    const esp_err_t err =
        attribute::update(s_endpointId, Thermostat::Id, attributeId, &value);
    if (err != ESP_OK) {
        ESP_LOGW(kTag, "Attribute update failed id=%lu err=%s",
                 static_cast<unsigned long>(attributeId), esp_err_to_name(err));
    }
}

}  // namespace

esp_err_t MatterPlatform::start(RuntimeController& runtime)
{
    if (s_running) return ESP_OK;
#if CONFIG_OPENCOMFORT_MATTER_DEVELOPMENT_CREDENTIALS
    ESP_LOGW(kTag, "Using development Matter credentials; not for production");
#endif
    s_runtime = &runtime;
    const RuntimeSnapshot snapshot = runtime.snapshot();

    node::config_t nodeConfig;
    node_t* node = node::create(&nodeConfig, attributeUpdate, identify);
    if (node == nullptr) {
        s_runtime = nullptr;
        return ESP_FAIL;
    }

    endpoint::thermostat::config_t thermostatConfig;
    thermostatConfig.thermostat.local_temperature =
        static_cast<int16_t>((snapshot.reported.valid ? snapshot.reported.returnAirC : 22.0f) *
                             100.0f);
    thermostatConfig.thermostat.system_mode = toMatterMode(snapshot.desired);
    thermostatConfig.thermostat.features.cooling.occupied_cooling_setpoint =
        static_cast<int16_t>(snapshot.desired.targetTempC * 100.0f);
    thermostatConfig.thermostat.features.heating.occupied_heating_setpoint =
        static_cast<int16_t>(snapshot.desired.targetTempC * 100.0f);
    thermostatConfig.thermostat.feature_flags =
        cluster::thermostat::feature::cooling::get_id() |
        cluster::thermostat::feature::heating::get_id() |
        cluster::thermostat::feature::auto_mode::get_id();

    endpoint_t* endpoint =
        endpoint::thermostat::create(node, &thermostatConfig, ENDPOINT_FLAG_NONE, nullptr);
    if (endpoint == nullptr) {
        s_runtime = nullptr;
        return ESP_FAIL;
    }
    s_endpointId = endpoint::get_id(endpoint);
    const esp_err_t err = esp_matter::start(deviceEvent);
    if (err != ESP_OK) {
        s_runtime = nullptr;
        s_endpointId = 0;
        return err;
    }
    s_running = true;
    s_publishedRevision = 0;
    sync();
    return ESP_OK;
}

void MatterPlatform::stop()
{
    // The ESP-Matter server has process lifetime. Disabling the connector is
    // applied after reboot, while this clears OpenComfort's callback bridge.
    s_runtime = nullptr;
    s_running = false;
}

void MatterPlatform::sync()
{
    if (!s_running || s_runtime == nullptr || s_endpointId == 0) return;
    const RuntimeSnapshot snapshot = s_runtime->snapshot();
    if (snapshot.revision == s_publishedRevision) return;

    updateAttribute(Thermostat::Attributes::SystemMode::Id,
                    esp_matter_enum8(toMatterMode(snapshot.desired)));
    const int16_t setpoint = static_cast<int16_t>(snapshot.desired.targetTempC * 100.0f);
    updateAttribute(Thermostat::Attributes::OccupiedCoolingSetpoint::Id,
                    esp_matter_int16(setpoint));
    updateAttribute(Thermostat::Attributes::OccupiedHeatingSetpoint::Id,
                    esp_matter_int16(setpoint));
    if (snapshot.reported.valid) {
        updateAttribute(Thermostat::Attributes::LocalTemperature::Id,
                        esp_matter_int16(
                            static_cast<int16_t>(snapshot.reported.returnAirC * 100.0f)));
    }
    s_publishedRevision = snapshot.revision;
}

esp_err_t MatterPlatform::openCommissioningWindow()
{
    if (!s_running) return ESP_ERR_INVALID_STATE;
    auto& manager = chip::Server::GetInstance().GetCommissioningWindowManager();
    if (manager.IsCommissioningWindowOpen()) return ESP_OK;
    const CHIP_ERROR err = manager.OpenBasicCommissioningWindow(
        kCommissioningWindow, chip::CommissioningWindowAdvertisement::kAllSupported);
    return err == CHIP_NO_ERROR ? ESP_OK : ESP_FAIL;
}

esp_err_t MatterPlatform::factoryReset()
{
    if (s_running) {
        esp_matter::factory_reset();
    }
    return ESP_OK;
}

bool MatterPlatform::isRunning()
{
    return s_running;
}

}  // namespace opencomfort
