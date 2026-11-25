#include "src/Runtime/RuntimeController.hpp"

#include <cmath>

#include "src/Modules/IModule.hpp"

namespace opencomfort {

RuntimeController::RuntimeController(IModule& module) : module_(module)
{
}

bool RuntimeController::init()
{
    if (queue_ != nullptr) {
        return true;
    }

    queue_ = xQueueCreateStatic(kRuntimeCommandQueueLength, sizeof(RuntimeCommand), queueStorage_,
                                &queueControl_);
    if (queue_ == nullptr) {
        return false;
    }

    RuntimeSnapshot initial = module_.runtimeSnapshot();
    initial.revision = 1;
    publishSnapshot(initial);
    return true;
}

RuntimeRequestStatus RuntimeController::requestDesiredState(const AcDesiredState& desired,
                                                            RuntimeCommandSource source,
                                                            uint32_t* requestId)
{
    if (queue_ == nullptr || !module_.isBooted()) {
        return RuntimeRequestStatus::NotReady;
    }
    if (!desiredStateIsValid(desired)) {
        return RuntimeRequestStatus::InvalidArgument;
    }

    RuntimeCommand command;
    portENTER_CRITICAL(&lock_);
    command.requestId = nextRequestId_++;
    portEXIT_CRITICAL(&lock_);
    command.source = source;
    command.desired = desired;

    if (xQueueSend(queue_, &command, 0) != pdTRUE) {
        return RuntimeRequestStatus::QueueFull;
    }
    if (requestId != nullptr) {
        *requestId = command.requestId;
    }
    return RuntimeRequestStatus::Accepted;
}

RuntimeSnapshot RuntimeController::snapshot() const
{
    portENTER_CRITICAL(&lock_);
    RuntimeSnapshot copy = snapshot_;
    portEXIT_CRITICAL(&lock_);
    return copy;
}

void RuntimeController::process()
{
    RuntimeCommand command;
    RuntimeCommand latest;
    bool hasCommand = false;
    while (queue_ != nullptr && xQueueReceive(queue_, &command, 0) == pdTRUE) {
        latest = command;
        hasCommand = true;
    }

    esp_err_t applyError = snapshot().lastApplyError;
    if (hasCommand) {
        applyError = module_.applyRuntimeDesired(latest.desired);
    }

    module_.run();

    RuntimeSnapshot next = module_.runtimeSnapshot();
    const RuntimeSnapshot previous = snapshot();
    next.revision = previous.revision;
    next.lastRequestId = previous.lastRequestId;
    next.lastCommandSource = previous.lastCommandSource;
    next.lastApplyError = applyError;
    if (hasCommand) {
        next.lastRequestId = latest.requestId;
        next.lastCommandSource = latest.source;
    }
    if (snapshotsDiffer(previous, next)) {
        next.revision = previous.revision + 1;
    }
    publishSnapshot(next);
}

bool RuntimeController::desiredStateIsValid(const AcDesiredState& desired)
{
    return std::isfinite(desired.targetTempC) &&
           static_cast<uint8_t>(desired.mode) <= static_cast<uint8_t>(AcMode::Auto) &&
           static_cast<uint8_t>(desired.fan) <= static_cast<uint8_t>(AcFanSpeed::High);
}

bool RuntimeController::snapshotsDiffer(const RuntimeSnapshot& left, const RuntimeSnapshot& right)
{
    return left.available != right.available || left.lastRequestId != right.lastRequestId ||
           left.lastCommandSource != right.lastCommandSource ||
           left.lastApplyError != right.lastApplyError ||
           left.desired.powerOn != right.desired.powerOn ||
           left.desired.mode != right.desired.mode ||
           left.desired.targetTempC != right.desired.targetTempC ||
           left.desired.fan != right.desired.fan || left.desired.vanes != right.desired.vanes ||
           left.reported.valid != right.reported.valid ||
           left.reported.returnAirC != right.reported.returnAirC ||
           left.reported.setTempC != right.reported.setTempC ||
           left.reported.hasPower != right.reported.hasPower ||
           left.reported.powerOn != right.reported.powerOn ||
           left.reported.hasMode != right.reported.hasMode || left.reported.mode != right.reported.mode ||
           left.reported.hasFan != right.reported.hasFan || left.reported.fan != right.reported.fan ||
           left.reported.hasVanes != right.reported.hasVanes ||
           left.reported.vanes != right.reported.vanes ||
           left.reported.hasCompressor != right.reported.hasCompressor ||
           left.reported.compressorRunning != right.reported.compressorRunning ||
           left.acCapabilities.vaneCount != right.acCapabilities.vaneCount ||
           left.acCapabilities.minTempC != right.acCapabilities.minTempC ||
           left.acCapabilities.maxTempC != right.acCapabilities.maxTempC;
}

void RuntimeController::publishSnapshot(const RuntimeSnapshot& snapshot)
{
    portENTER_CRITICAL(&lock_);
    snapshot_ = snapshot;
    portEXIT_CRITICAL(&lock_);
}

}  // namespace opencomfort
