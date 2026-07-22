#pragma once

#include "src/Config/UserConfig.hpp"
#include "src/Product/ProductDefinition.hpp"

namespace opencomfort {

class CaptivePortal {
public:
    explicit CaptivePortal(const ProductDefinition& product);
    ~CaptivePortal();

    // Blocks until provisioning completes successfully or fails hard.
    bool run(UserConfig& outConfig);

private:
    const ProductDefinition& product_;
};

}  // namespace opencomfort
