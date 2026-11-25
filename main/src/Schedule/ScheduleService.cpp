#include "src/Schedule/ScheduleService.hpp"

#include <cstdlib>
#include <cstring>

#include "src/Runtime/RuntimeController.hpp"

extern "C" {
#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "esp_timer.h"
#include "nvs.h"
}

namespace opencomfort {
namespace {

constexpr const char* kTag = "Schedule";
// Same namespace as the user config, so a factory reset clears the schedule too.
constexpr const char* kNamespace = "oc_user";
constexpr const char* kKey = "schedule";
constexpr uint8_t kVersion = 1;

struct StoredSchedule {
    uint8_t version = kVersion;
    ScheduleConfig config{};
};

bool validRule(const ScheduleRule& rule)
{
    return rule.hour < 24 && rule.minute < 60 && (rule.days & 0x7F) != 0 &&
           static_cast<uint8_t>(rule.mode) <= static_cast<uint8_t>(AcMode::Auto) &&
           rule.targetTempC >= 10 && rule.targetTempC <= 35;
}

void applyTimezone(const char* tz)
{
    setenv("TZ", tz, 1);
    tzset();
}

}  // namespace

bool scheduleRuleDue(const ScheduleRule& rule, const std::tm& now)
{
    if (!rule.enabled || now.tm_hour != rule.hour || now.tm_min != rule.minute) {
        return false;
    }
    const int mondayFirst = (now.tm_wday + 6) % 7;  // tm_wday: 0 = Sunday
    return (rule.days & (1u << mondayFirst)) != 0;
}

AcDesiredState scheduleRuleApply(const ScheduleRule& rule, const AcDesiredState& current)
{
    AcDesiredState next = current;
    if (rule.action == ScheduleAction::TurnOff) {
        next.powerOn = false;
        return next;
    }
    next.powerOn = true;
    next.mode = rule.mode == AcMode::Off ? AcMode::Auto : rule.mode;
    next.targetTempC = static_cast<float>(rule.targetTempC);
    return next;
}

ScheduleService& ScheduleService::instance()
{
    static ScheduleService service;
    return service;
}

void ScheduleService::begin()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        nvs_handle_t handle = 0;
        if (nvs_open(kNamespace, NVS_READONLY, &handle) == ESP_OK) {
            StoredSchedule stored;
            size_t size = sizeof(stored);
            if (nvs_get_blob(handle, kKey, &stored, &size) == ESP_OK && size == sizeof(stored) &&
                stored.version == kVersion && stored.config.ruleCount <= kMaxScheduleRules) {
                config_ = stored.config;
                config_.timezone[kMaxTimezoneLen] = '\0';
            }
            nvs_close(handle);
        }
        applyTimezone(config_.timezone);
    }
    if (!sntpStarted_) {
        esp_sntp_config_t sntp = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
        sntpStarted_ = esp_netif_sntp_init(&sntp) == ESP_OK;
        if (!sntpStarted_) {
            ESP_LOGW(kTag, "SNTP start failed; timed rules stay idle until the clock is set");
        }
    }
}

bool ScheduleService::timeValid() const
{
    // Before SNTP the clock starts at 1970.
    return std::time(nullptr) > 1700000000;
}

void ScheduleService::tick(RuntimeController& runtime)
{
    const int64_t nowUs = esp_timer_get_time();
    bool sleepDue = false;
    ScheduleConfig cfg;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (sleepUntilUs_ != 0 && nowUs >= sleepUntilUs_) {
            sleepUntilUs_ = 0;
            sleepDue = true;
        }
        cfg = config_;
    }
    if (sleepDue) {
        AcDesiredState off = runtime.snapshot().desired;
        off.powerOn = false;
        (void)runtime.requestDesiredState(off, RuntimeCommandSource::Schedule);
    }

    if (!timeValid()) {
        return;
    }
    const std::time_t now = std::time(nullptr);
    std::tm local{};
    localtime_r(&now, &local);
    const int minuteKey = local.tm_yday * 1440 + local.tm_hour * 60 + local.tm_min;
    if (minuteKey == lastMinuteKey_) {
        return;
    }
    lastMinuteKey_ = minuteKey;

    for (size_t i = 0; i < cfg.ruleCount; ++i) {
        if (!scheduleRuleDue(cfg.rules[i], local)) {
            continue;
        }
        const AcDesiredState next = scheduleRuleApply(cfg.rules[i], runtime.snapshot().desired);
        if (runtime.requestDesiredState(next, RuntimeCommandSource::Schedule) !=
            RuntimeRequestStatus::Accepted) {
            ESP_LOGW(kTag, "Rule %u could not be queued", static_cast<unsigned>(i));
        }
    }
}

ScheduleConfig ScheduleService::config()
{
    std::lock_guard<std::mutex> lock(mutex_);
    return config_;
}

esp_err_t ScheduleService::setConfig(const ScheduleConfig& config)
{
    if (config.ruleCount > kMaxScheduleRules || config.timezone[0] == '\0' ||
        std::memchr(config.timezone, '\0', sizeof(config.timezone)) == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    for (size_t i = 0; i < config.ruleCount; ++i) {
        if (!validRule(config.rules[i])) {
            return ESP_ERR_INVALID_ARG;
        }
    }

    StoredSchedule stored;
    stored.config = config;
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(kNamespace, NVS_READWRITE, &handle);
    if (err == ESP_OK) {
        err = nvs_set_blob(handle, kKey, &stored, sizeof(stored));
        if (err == ESP_OK) err = nvs_commit(handle);
        nvs_close(handle);
    }
    if (err != ESP_OK) {
        return err;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    config_ = config;
    applyTimezone(config_.timezone);
    return ESP_OK;
}

void ScheduleService::setSleepMinutes(uint32_t minutes)
{
    std::lock_guard<std::mutex> lock(mutex_);
    sleepUntilUs_ = minutes == 0 ? 0 : esp_timer_get_time() + static_cast<int64_t>(minutes) * 60000000LL;
}

uint32_t ScheduleService::sleepSecondsLeft() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (sleepUntilUs_ == 0) {
        return 0;
    }
    const int64_t left = sleepUntilUs_ - esp_timer_get_time();
    return left > 0 ? static_cast<uint32_t>(left / 1000000) : 0;
}

}  // namespace opencomfort
