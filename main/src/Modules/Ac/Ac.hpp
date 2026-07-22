#pragma once

#include "src/Bus/BusFactory.hpp"
#include "src/Config/UserConfig.hpp"
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

private:
    const ProductDefinition& product_;
    UserConfig config_;
    LoadedAcProfile profile_{};
    BusHandle bus_{};
    bool booted_ = false;
};

}  // namespace opencomfort
