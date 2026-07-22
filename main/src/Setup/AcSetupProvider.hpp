#pragma once

#include "src/Setup/ISetupProvider.hpp"

namespace opencomfort {

class AcSetupProvider final : public ISetupProvider {
public:
    const char* moduleId() const override;
    bool buildSchema(SetupSchema& out) override;
    bool validateDraft(const SetupDraft& draft, char* error, size_t errorLen) const override;
    bool finalize(const SetupDraft& draft, UserConfig& out, char* error,
                  size_t errorLen) const override;
};

ISetupProvider& currentSetupProvider();

}  // namespace opencomfort
