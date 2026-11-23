#pragma once

#include <array>
#include <memory>
#include <string>

#include "src/Busses/IBus.hpp"
#include "src/Configs/ModuleConfig.hpp"
#include "src/Modules/IModule.hpp"
#include "src/Modules/Ac/Commands/ProfileCommandEngine.hpp"
#include "src/Modules/Ac/Models/AcProfile.hpp"
#include "src/Modules/Ac/Models/State.hpp"

struct AcCommand {
    bool hasPower = false;
    bool powerOn = false;
    bool hasMode = false;
    Mode mode = Mode::Off;
    bool hasTargetTemp = false;
    float targetTemp = 22.0f;
    bool hasFanSpeed = false;
    FanSpeed fanSpeed = FanSpeed::Auto;
    bool hasHorizontalVane = false;
    FanControlHorizontal horizontalVane = FanControlHorizontal::Center;
    bool hasVerticalVane = false;
    FanControlVertical verticalVane = FanControlVertical::Down_Center;
};

class Ac : public IModule {
public:
    Ac() = default;
    explicit Ac(const AcModuleConfig& config);
    ~Ac() override = default;

    void bootstrap() override;
    bool isBooted() const override { return booted_; }
    void run() override;

    const State& state() const { return state_; }
    const AcModuleConfig& config() const { return config_; }
    const AcProfile& profile() const { return profile_; }
    bool applyCommand(const AcCommand& command, std::string* error = nullptr);

private:
    bool initBus(std::string* error);
    bool sendPayload(std::string* error);
    bool executeProfileCommand(const std::string& name, const CommandVars& vars, std::string* error);
    void pollTelemetry();

    AcModuleConfig config_;
    AcProfile profile_;
    State state_;
    std::array<uint8_t, ProfileCommandEngine::kDbFieldCount> dbFields_{};
    std::unique_ptr<IBus> bus_;
    bool booted_ = false;
    uint32_t runTicks_ = 0;
};
