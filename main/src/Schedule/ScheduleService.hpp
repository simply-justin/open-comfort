#pragma once

#include <cstddef>
#include <cstdint>
#include <ctime>
#include <mutex>

#include "esp_err.h"

#include "src/Modules/Ac/AcState.hpp"

namespace opencomfort {

class RuntimeController;

constexpr size_t kMaxScheduleRules = 8;
constexpr size_t kMaxTimezoneLen = 47;

enum class ScheduleAction : uint8_t {
    TurnOff = 0,
    TurnOn = 1,
};

struct ScheduleRule {
    bool enabled = true;
    uint8_t hour = 7;
    uint8_t minute = 0;
    uint8_t days = 0x7F;  // bit 0 = Monday ... bit 6 = Sunday
    ScheduleAction action = ScheduleAction::TurnOn;
    AcMode mode = AcMode::Cool;
    int8_t targetTempC = 22;
};

struct ScheduleConfig {
    char timezone[kMaxTimezoneLen + 1] = "CET-1CEST,M3.5.0,M10.5.0/3";  // POSIX TZ
    uint8_t ruleCount = 0;
    ScheduleRule rules[kMaxScheduleRules] = {};
};

// True when `rule` should fire at local time `now` (minute resolution).
bool scheduleRuleDue(const ScheduleRule& rule, const std::tm& now);
// The state a rule produces from the current one; untouched fields are kept.
AcDesiredState scheduleRuleApply(const ScheduleRule& rule, const AcDesiredState& current);

// Runs timers on the device: rules at a wall-clock time (needs SNTP) and a sleep timer.
// tick() runs on the main loop; the getters and setters are called from the HTTP task.
class ScheduleService {
public:
    static ScheduleService& instance();

    void begin();  // loads rules, applies the time zone, starts SNTP
    void tick(RuntimeController& runtime);

    ScheduleConfig config();
    esp_err_t setConfig(const ScheduleConfig& config);
    bool timeValid() const;

    void setSleepMinutes(uint32_t minutes);  // 0 cancels
    uint32_t sleepSecondsLeft() const;

private:
    ScheduleService() = default;

    mutable std::mutex mutex_;
    ScheduleConfig config_{};
    int lastMinuteKey_ = -1;
    int64_t sleepUntilUs_ = 0;
    bool sntpStarted_ = false;
};

}  // namespace opencomfort
