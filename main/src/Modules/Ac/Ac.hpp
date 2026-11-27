#pragma once

#include <cmath>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"

#include "src/Bus/BusFactory.hpp"
#include "src/Config/UserConfig.hpp"
#include "src/Modules/Ac/AcProtocolEngine.hpp"
#include "src/Modules/Ac/AcState.hpp"
#include "src/Modules/IModule.hpp"
#include "src/Product/ProductDefinition.hpp"
#include "src/Profiles/ProfileTypes.hpp"

namespace opencomfort {

class AcModule final : public IModule {
public:
    AcModule(const ProductDefinition& product, UserConfig config);
    ~AcModule() override = default;

    void bootstrap() override;
    bool isBooted() const override;
    void run() override;

    esp_err_t applyRuntimeDesired(const AcDesiredState& desired) override;
    RuntimeSnapshot runtimeSnapshot() const override;

    // Compatibility wrapper. Runtime connectors must use RuntimeController.
    esp_err_t setDesiredState(const AcDesiredState& desired);
    AcDesiredState desiredState() const;
    AcReportedState reportedState() const;

private:
    void holdRoomTemp(AcReportedState& state);
    void applyReading(bool ok, AcReportedState latest);
    bool isValidDesired(const AcDesiredState& desired) const;

    const ProductDefinition& product_;
    UserConfig config_;
    LoadedAcProfile profile_{};
    BusHandle bus_{};
    AcProtocolEngine protocol_{};
    AcDesiredState desired_{};
    AcReportedState reported_{};
    float shownRoomC_ = NAN;  // room temperature as last reported, see holdRoomTemp()
    TickType_t nextPoll_ = 0;
    TickType_t lastValid_ = 0;
    bool mhiLink_ = false;
    bool booted_ = false;
};

}  // namespace opencomfort
