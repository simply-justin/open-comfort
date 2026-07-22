#pragma once

#include <memory>

#include "src/Config/UserConfig.hpp"
#include "src/Modules/IModule.hpp"
#include "src/Product/ProductDefinition.hpp"

namespace opencomfort {

class ModuleFactory {
public:
    static std::unique_ptr<IModule> create(const ProductDefinition& product,
                                           const UserConfig& config);
};

}  // namespace opencomfort
