#pragma once

#include "esp_err.h"

#include "src/Modules/Ac/AcState.hpp"
#include "src/Runtime/RuntimeTypes.hpp"

namespace opencomfort {

class IModule {
public:
    virtual ~IModule() = default;
    virtual void bootstrap() = 0;
    virtual bool isBooted() const = 0;
    virtual void run() = 0;

    // Runtime commands are invoked only by RuntimeController from the
    // application task, keeping transport ownership single-threaded.
    virtual esp_err_t applyRuntimeDesired(const AcDesiredState& desired) = 0;
    virtual RuntimeSnapshot runtimeSnapshot() const = 0;
};

}  // namespace opencomfort
