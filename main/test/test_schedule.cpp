#include "unity.h"
#include "unity_test_runner.h"

#include "src/Schedule/ScheduleService.hpp"

namespace opencomfort {

TEST_CASE("schedule rules fire on their weekday and minute only", "[schedule]")
{
    ScheduleRule rule;
    rule.hour = 7;
    rule.minute = 30;
    rule.days = 0x1F;  // Monday to Friday

    std::tm now{};
    now.tm_hour = 7;
    now.tm_min = 30;
    now.tm_wday = 1;  // Monday
    TEST_ASSERT_TRUE(scheduleRuleDue(rule, now));
    now.tm_wday = 0;  // Sunday
    TEST_ASSERT_FALSE(scheduleRuleDue(rule, now));
    now.tm_wday = 5;  // Friday
    TEST_ASSERT_TRUE(scheduleRuleDue(rule, now));
    now.tm_min = 31;
    TEST_ASSERT_FALSE(scheduleRuleDue(rule, now));
    now.tm_min = 30;
    rule.enabled = false;
    TEST_ASSERT_FALSE(scheduleRuleDue(rule, now));
}

TEST_CASE("schedule rules change only what they control", "[schedule]")
{
    AcDesiredState current;
    current.powerOn = true;
    current.mode = AcMode::Heat;
    current.targetTempC = 24.0f;
    current.fan = AcFanSpeed::High;

    ScheduleRule off;
    off.action = ScheduleAction::TurnOff;
    AcDesiredState next = scheduleRuleApply(off, current);
    TEST_ASSERT_FALSE(next.powerOn);
    TEST_ASSERT_EQUAL(static_cast<int>(AcMode::Heat), static_cast<int>(next.mode));
    TEST_ASSERT_EQUAL_FLOAT(24.0f, next.targetTempC);

    ScheduleRule on;
    on.action = ScheduleAction::TurnOn;
    on.mode = AcMode::Cool;
    on.targetTempC = 21;
    next = scheduleRuleApply(on, current);
    TEST_ASSERT_TRUE(next.powerOn);
    TEST_ASSERT_EQUAL(static_cast<int>(AcMode::Cool), static_cast<int>(next.mode));
    TEST_ASSERT_EQUAL_FLOAT(21.0f, next.targetTempC);
    TEST_ASSERT_EQUAL(static_cast<int>(AcFanSpeed::High), static_cast<int>(next.fan));
}

}  // namespace opencomfort

#include "src/Config/FactoryReset.hpp"

namespace opencomfort {

TEST_CASE("only consecutive power-on resets count towards a factory reset", "[reset]")
{
    TEST_ASSERT_EQUAL_UINT8(1, FactoryReset::nextPowerCycleCount(0, true));
    TEST_ASSERT_EQUAL_UINT8(5, FactoryReset::nextPowerCycleCount(4, true));
    // A crash, watchdog or brownout reset starts the sequence over.
    TEST_ASSERT_EQUAL_UINT8(0, FactoryReset::nextPowerCycleCount(4, false));
    TEST_ASSERT_EQUAL_UINT8(255, FactoryReset::nextPowerCycleCount(255, true));
}

}  // namespace opencomfort

#include "src/Update/UpdateService.hpp"

namespace opencomfort {

TEST_CASE("release versions compare and development builds are not releases", "[update]")
{
    // Unity here has no 64-bit asserts, so compare as booleans.
    TEST_ASSERT_TRUE(parseReleaseVersion("v0.2.0") > parseReleaseVersion("v0.1.9"));
    TEST_ASSERT_TRUE(parseReleaseVersion("1.10.0") > parseReleaseVersion("1.9.9"));
    TEST_ASSERT_TRUE(parseReleaseVersion("v1.2.3") == parseReleaseVersion("1.2.3"));
    TEST_ASSERT_TRUE(parseReleaseVersion("v0.3.0-alpha.1") < parseReleaseVersion("v0.3.0-beta"));
    TEST_ASSERT_TRUE(parseReleaseVersion("v0.3.0-beta.1") < parseReleaseVersion("v0.3.0-beta.2"));
    TEST_ASSERT_TRUE(parseReleaseVersion("v0.3.0-beta.9") < parseReleaseVersion("v0.3.0-rc.1"));
    TEST_ASSERT_TRUE(parseReleaseVersion("v0.3.0-rc.1") < parseReleaseVersion("v0.3.0"));
    TEST_ASSERT_TRUE(parseReleaseVersion("v0.3.0-beta.1") > parseReleaseVersion("v0.2.9"));
    TEST_ASSERT_TRUE(parseReleaseVersion("2868f98-dirty") < 0);
    TEST_ASSERT_TRUE(parseReleaseVersion("v1.2.3-4-gabc1234") < 0);
    TEST_ASSERT_TRUE(parseReleaseVersion("v1.2.3-beta.x") < 0);
    TEST_ASSERT_TRUE(parseReleaseVersion("") < 0);
    TEST_ASSERT_TRUE(parseReleaseVersion(nullptr) < 0);
}

}  // namespace opencomfort
