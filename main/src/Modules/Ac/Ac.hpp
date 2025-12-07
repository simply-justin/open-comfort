#pragma once

#include <string>
#include "src/Modules/IModule.hpp"

class Ac : public IModule {
public:
    Ac() = default;
    ~Ac() override = default;

    void bootstrap() override;
    bool isBooted() const override { return booted_; }
    void run() override;

private:
    bool booted_ = false;
};
