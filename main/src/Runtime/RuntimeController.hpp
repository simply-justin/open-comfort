#pragma once

#include <cstdint>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include "src/Runtime/RuntimeTypes.hpp"

namespace opencomfort {

class IModule;

class RuntimeController {
public:
    explicit RuntimeController(IModule& module);
    ~RuntimeController() = default;

    RuntimeController(const RuntimeController&) = delete;
    RuntimeController& operator=(const RuntimeController&) = delete;

    bool init();
    RuntimeRequestStatus requestDesiredState(const AcDesiredState& desired,
                                             RuntimeCommandSource source,
                                             uint32_t* requestId = nullptr);
    RuntimeSnapshot snapshot() const;

    // Called only by the application task. This is the sole path that may
    // execute module bus operations.
    void process();

private:
    static bool desiredStateIsValid(const AcDesiredState& desired);
    static bool snapshotsDiffer(const RuntimeSnapshot& left, const RuntimeSnapshot& right);
    void publishSnapshot(const RuntimeSnapshot& snapshot);

    IModule& module_;
    QueueHandle_t queue_ = nullptr;
    StaticQueue_t queueControl_{};
    uint8_t queueStorage_[kRuntimeCommandQueueLength * sizeof(RuntimeCommand)] = {};
    mutable portMUX_TYPE lock_ = portMUX_INITIALIZER_UNLOCKED;
    RuntimeSnapshot snapshot_{};
    uint32_t nextRequestId_ = 1;
};

}  // namespace opencomfort
