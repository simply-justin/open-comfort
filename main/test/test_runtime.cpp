#include "unity.h"

#include "src/Config/UserConfigStore.hpp"
#include "src/Modules/IModule.hpp"
#include "src/Runtime/RuntimeController.hpp"

namespace opencomfort {
namespace {

class MockModule final : public IModule {
public:
    void bootstrap() override { booted = true; }
    bool isBooted() const override { return booted; }
    void run() override { runCount++; }

    esp_err_t applyRuntimeDesired(const AcDesiredState& desired) override
    {
        applyCount++;
        snapshotValue.desired = desired;
        return applyResult;
    }

    RuntimeSnapshot runtimeSnapshot() const override
    {
        RuntimeSnapshot value = snapshotValue;
        value.available = booted;
        return value;
    }

    bool booted = true;
    int runCount = 0;
    int applyCount = 0;
    esp_err_t applyResult = ESP_OK;
    RuntimeSnapshot snapshotValue{};
};

}  // namespace

TEST_CASE("runtime controller coalesces command bursts", "[runtime]")
{
    MockModule module;
    RuntimeController runtime(module);
    TEST_ASSERT_TRUE(runtime.init());

    AcDesiredState first;
    first.targetTempC = 20.0f;
    AcDesiredState latest = first;
    latest.targetTempC = 24.0f;

    TEST_ASSERT_EQUAL(static_cast<int>(RuntimeRequestStatus::Accepted),
                      static_cast<int>(runtime.requestDesiredState(
                          first, RuntimeCommandSource::LocalWeb)));
    TEST_ASSERT_EQUAL(static_cast<int>(RuntimeRequestStatus::Accepted),
                      static_cast<int>(runtime.requestDesiredState(
                          latest, RuntimeCommandSource::HomeAssistantMqtt)));
    runtime.process();

    TEST_ASSERT_EQUAL(1, module.applyCount);
    TEST_ASSERT_EQUAL_FLOAT(24.0f, module.snapshotValue.desired.targetTempC);
    TEST_ASSERT_EQUAL(1, module.runCount);
    TEST_ASSERT_EQUAL(static_cast<int>(RuntimeCommandSource::HomeAssistantMqtt),
                      static_cast<int>(runtime.snapshot().lastCommandSource));
}

TEST_CASE("runtime controller rejects invalid states", "[runtime]")
{
    MockModule module;
    RuntimeController runtime(module);
    TEST_ASSERT_TRUE(runtime.init());

    AcDesiredState desired;
    desired.mode = static_cast<AcMode>(255);
    TEST_ASSERT_EQUAL(static_cast<int>(RuntimeRequestStatus::InvalidArgument),
                      static_cast<int>(runtime.requestDesiredState(
                          desired, RuntimeCommandSource::LocalWeb)));
}

TEST_CASE("user config v1 migration preserves product settings", "[config]")
{
    UserConfigV1 legacy;
    legacy.provisioned = true;
    copyBounded(legacy.friendlyName, sizeof(legacy.friendlyName), "Bedroom");
    copyBounded(legacy.ac.profileId, sizeof(legacy.ac.profileId), "profile");
    copyBounded(legacy.ac.modelId, sizeof(legacy.ac.modelId), "model");
    legacy.ac.bus = BusKind::Spi;

    const UserConfig migrated = migrateUserConfigV1(legacy);
    TEST_ASSERT_EQUAL(kUserConfigSchemaVersion, migrated.schemaVersion);
    TEST_ASSERT_TRUE(migrated.provisioned);
    TEST_ASSERT_EQUAL_STRING("Bedroom", migrated.friendlyName);
    TEST_ASSERT_EQUAL_STRING("profile", migrated.ac.profileId);
    TEST_ASSERT_TRUE(migrated.connectors.localWebEnabled);
    TEST_ASSERT_FALSE(migrated.connectors.mqttEnabled);
    TEST_ASSERT_FALSE(migrated.connectors.matterEnabled);
}

}  // namespace opencomfort
