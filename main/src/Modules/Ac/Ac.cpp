#include "src/Modules/Ac/Ac.hpp"

extern "C" {
    #include "esp_log.h"
}

#include <type_traits>
#include <vector>

#include "src/Busses/BusFactory.hpp"
#include "src/Modules/Ac/AcProfileLoader.hpp"

static const char* TAG = "MODULE_AC";

Ac::Ac(const AcModuleConfig& config)
    : config_(config)
{}

bool Ac::initBus(std::string* error) {
    bool busMatches = false;
    std::visit([&](auto& busCfg) {
        using T = std::decay_t<decltype(busCfg)>;
        if constexpr (std::is_same_v<T, SpiConfig>) {
            busMatches = profile_.busType == BusType::SPI;
            busCfg.mode = profile_.spiMode;
        } else if constexpr (std::is_same_v<T, UartConfig>) {
            busMatches = profile_.busType == BusType::UART || profile_.busType == BusType::RS485;
        } else if constexpr (std::is_same_v<T, CanConfig>) {
            busMatches = profile_.busType == BusType::CAN;
        }
    }, config_.bus);

    if (!busMatches) {
        if (error) *error = "configured bus does not match profile protocol";
        return false;
    }

    bus_ = BusFactory::create(config_.bus);
    if (!bus_) {
        if (error) *error = "failed to create bus instance";
        return false;
    }

    if (!bus_->init()) {
        if (error) *error = "failed to initialize bus hardware";
        bus_.reset();
        return false;
    }

    ESP_LOGI(TAG, "Bus initialized for profile %s", profile_.brand.c_str());
    return true;
}

bool Ac::sendPayload(std::string* error) {
    if (!bus_) {
        if (error) *error = "bus is not initialized";
        return false;
    }

    const std::vector<uint8_t> payload = ProfileCommandEngine::buildPayload(profile_, dbFields_);
    if (!bus_->write(payload.data(), payload.size())) {
        if (error) *error = "bus write failed";
        return false;
    }

    ESP_LOGI(TAG, "Sent AC payload (%u bytes)", (unsigned)payload.size());
    return true;
}

bool Ac::executeProfileCommand(const std::string& name, const CommandVars& vars, std::string* error) {
    return ProfileCommandEngine::applyProfileCommand(profile_, name, vars, dbFields_, error);
}

void Ac::bootstrap() {
    ESP_LOGI(TAG, "Booting AC module...");

    if (config_.acProfilePath.empty()) {
        ESP_LOGE(TAG, "Missing AC profile path");
        booted_ = false;
        return;
    }

    std::string error;
    if (!AcProfileLoader::load(config_.acProfilePath, profile_, &error)) {
        ESP_LOGE(TAG, "Failed to load profile: %s", error.c_str());
        booted_ = false;
        return;
    }

    dbFields_.fill(0);

    if (!initBus(&error)) {
        ESP_LOGE(TAG, "Bus setup failed: %s", error.c_str());
        booted_ = false;
        return;
    }

    booted_ = true;
    ESP_LOGI(TAG, "AC module booted with profile %s", profile_.brand.c_str());
}

void Ac::pollTelemetry() {
    if (!bus_ || profile_.dataQueries.empty()) return;

    const DataQuery* query = nullptr;
    for (const auto& candidate : profile_.dataQueries) {
        if (candidate.name == "RETURN-AIR" || candidate.name == "SET-TEMP") {
            query = &candidate;
            break;
        }
    }
    if (!query) return;

    if (!bus_->write(query->bytes.data(), query->bytes.size())) {
        ESP_LOGW(TAG, "Telemetry query write failed for %s", query->name.c_str());
        return;
    }

    uint8_t buffer[32] = {};
    const int received = bus_->read(buffer, sizeof(buffer));
    if (received <= 0) {
        ESP_LOGD(TAG, "No telemetry response for %s", query->name.c_str());
        return;
    }

    if (received >= 3) {
        const float reported = static_cast<float>(buffer[2]) / 2.0f + static_cast<float>(profile_.minTemp);
        state_.currentTemp = reported;
        ESP_LOGD(TAG, "Telemetry %s current temp %.1f C", query->name.c_str(), state_.currentTemp);
    }
}

void Ac::run() {
    if (!booted_) {
        ESP_LOGW(TAG, "run() called before module boot");
        return;
    }

    ++runTicks_;
    if (runTicks_ % 500 == 0) {
        pollTelemetry();
    }
}

bool Ac::applyCommand(const AcCommand& command, std::string* error) {
    if (!booted_) {
        if (error) *error = "AC module is not booted";
        return false;
    }

    if (command.hasTargetTemp &&
        (command.targetTemp < static_cast<float>(profile_.minTemp) ||
         command.targetTemp > static_cast<float>(profile_.maxTemp))) {
        if (error) *error = "target temperature out of supported range";
        return false;
    }

    if (command.hasPower) state_.powerOn = command.powerOn;
    if (command.hasMode) state_.mode = command.mode;
    if (command.hasTargetTemp) state_.targetTemp = command.targetTemp;
    if (command.hasFanSpeed) state_.fanSpeed = command.fanSpeed;
    if (command.hasHorizontalVane) state_.fanHorizontal = command.horizontalVane;
    if (command.hasVerticalVane) state_.fanVertical = command.verticalVane;

    bool sent = false;

    if (command.hasPower) {
        CommandVars vars;
        vars.hasPower = true;
        vars.powerOn = state_.powerOn;
        if (!executeProfileCommand("set_power", vars, error)) return false;
        sent = true;
    }

    if (command.hasMode) {
        CommandVars vars;
        vars.hasMode = true;
        vars.mode = state_.mode;
        if (!executeProfileCommand("set_mode", vars, error)) return false;
        sent = true;
    }

    if (command.hasTargetTemp) {
        CommandVars vars;
        vars.hasTemp = true;
        vars.tempC = state_.targetTemp;
        if (!executeProfileCommand("set_temperature", vars, error)) return false;
        sent = true;
    }

    if (command.hasFanSpeed) {
        CommandVars vars;
        vars.hasFan = true;
        vars.fan = state_.fanSpeed;
        if (!executeProfileCommand("set_fan_speed", vars, error)) return false;
        sent = true;
    }

    if (!sent) {
        if (error) *error = "no supported command fields provided";
        return false;
    }

    return sendPayload(error);
}
