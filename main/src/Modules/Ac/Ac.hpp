#pragma once

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
    const ProductDefinition& product_;
    UserConfig config_;
    LoadedAcProfile profile_{};
    BusHandle bus_{};
    AcProtocolEngine protocol_{};
    AcDesiredState desired_{};
    AcReportedState reported_{};
    TickType_t nextPoll_ = 0;
    bool booted_ = false;
};

}  // namespace opencomfort
