#include "unity.h"
#include "unity_test_runner.h"

#include <cstdio>
#include <cstring>

#include "src/Config/UserConfigStore.hpp"
#include "src/Config/DeviceHostname.hpp"
#include "src/Connectors/Web/RuntimeHttpConnector.hpp"
#include "nvs.h"

namespace opencomfort {

TEST_CASE("session cookie value is matched exactly", "[web]")
{
    const char* token = "0123456789abcdef0123456789abcdef";
    char header[128];

    std::snprintf(header, sizeof(header), "oc_session=%s", token);
    const char* value = RuntimeHttpConnector::sessionCookieValue(header);
    TEST_ASSERT_NOT_NULL(value);
    TEST_ASSERT_EQUAL(0, std::strncmp(value, token, 32));

    std::snprintf(header, sizeof(header), "theme=dark; oc_session=%s; lang=nl", token);
    value = RuntimeHttpConnector::sessionCookieValue(header);
    TEST_ASSERT_NOT_NULL(value);
    TEST_ASSERT_EQUAL(0, std::strncmp(value, token, 32));

    // Another cookie whose name merely ends in oc_session must not count.
    std::snprintf(header, sizeof(header), "xoc_session=%s", token);
    TEST_ASSERT_NULL(RuntimeHttpConnector::sessionCookieValue(header));

    // Wrong length is rejected, both shorter and longer.
    TEST_ASSERT_NULL(RuntimeHttpConnector::sessionCookieValue("oc_session=abc"));
    std::snprintf(header, sizeof(header), "oc_session=%sff", token);
    TEST_ASSERT_NULL(RuntimeHttpConnector::sessionCookieValue(header));
    TEST_ASSERT_NULL(RuntimeHttpConnector::sessionCookieValue(""));
}

TEST_CASE("AC state survives a restart and is cleared by a reset", "[config]")
{
    TEST_ASSERT_TRUE(UserConfigStore::init());
    TEST_ASSERT_EQUAL(ConfigStatus::Ok, UserConfigStore::clear());

    AcDesiredState loaded{};
    TEST_ASSERT_FALSE(UserConfigStore::loadAcDesired(loaded));

    AcDesiredState saved{};
    saved.powerOn = true;
    saved.mode = AcMode::Heat;
    saved.targetTempC = 23.0f;
    saved.fan = AcFanSpeed::High;
    TEST_ASSERT_EQUAL(ConfigStatus::Ok, UserConfigStore::saveAcDesired(saved));

    TEST_ASSERT_TRUE(UserConfigStore::loadAcDesired(loaded));
    TEST_ASSERT_TRUE(loaded.powerOn);
    TEST_ASSERT_EQUAL(static_cast<int>(AcMode::Heat), static_cast<int>(loaded.mode));
    TEST_ASSERT_EQUAL_FLOAT(23.0f, loaded.targetTempC);
    TEST_ASSERT_EQUAL(static_cast<int>(AcFanSpeed::High), static_cast<int>(loaded.fan));

    TEST_ASSERT_EQUAL(ConfigStatus::Ok, UserConfigStore::clear());
    TEST_ASSERT_FALSE(UserConfigStore::loadAcDesired(loaded));
}

TEST_CASE("local hostname validation normalizes names without accepting domains or URLs", "[config]")
{
    char hostname[kMaxHostnameLen + 1] = {};
    TEST_ASSERT_TRUE(normalizeDeviceHostname("Airco-Woonkamer.LOCAL", hostname, sizeof(hostname)));
    TEST_ASSERT_EQUAL_STRING("airco-woonkamer", hostname);
    TEST_ASSERT_TRUE(normalizeDeviceHostname("", hostname, sizeof(hostname)));
    TEST_ASSERT_EQUAL_STRING("", hostname);
    TEST_ASSERT_TRUE(normalizeDeviceHostname("a", hostname, sizeof(hostname)));
    TEST_ASSERT_EQUAL_STRING("a", hostname);
    for (const char* invalid : {"-airco", "airco-", "air_co", "air co", ".local",
                               "airco.example.com", "http://airco.local", "airco/local"}) {
        TEST_ASSERT_FALSE(normalizeDeviceHostname(invalid, hostname, sizeof(hostname)));
    }
    char longest[kMaxHostnameLen + 7];
    std::memset(longest, 'a', kMaxHostnameLen);
    std::strcpy(longest + kMaxHostnameLen, ".local");
    TEST_ASSERT_TRUE(normalizeDeviceHostname(longest, hostname, sizeof(hostname)));
    TEST_ASSERT_EQUAL(kMaxHostnameLen, std::strlen(hostname));
    longest[kMaxHostnameLen] = 'a';
    longest[kMaxHostnameLen + 1] = '\0';
    TEST_ASSERT_FALSE(normalizeDeviceHostname(longest, hostname, sizeof(hostname)));
    TEST_ASSERT_FALSE(normalizeDeviceHostname("airco", hostname, 3));
}

TEST_CASE("schema v2 settings migrate and the local hostname persists", "[config]")
{
    TEST_ASSERT_TRUE(UserConfigStore::init());
    TEST_ASSERT_EQUAL(ConfigStatus::Ok, UserConfigStore::clear());
    UserConfigV2 legacy;
    legacy.provisioned = true;
    copyBounded(legacy.friendlyName, sizeof(legacy.friendlyName), "Living room");
    copyBounded(legacy.ac.profileId, sizeof(legacy.ac.profileId), "mitsubishi_heavy_industries");
    copyBounded(legacy.ac.modelId, sizeof(legacy.ac.modelId), "SRK25ZSX");
    legacy.ac.bus = BusKind::Spi;
    legacy.connectors.mqttEnabled = true;
    copyBounded(legacy.connectors.mqttHost, sizeof(legacy.connectors.mqttHost), "broker.local");

    nvs_handle_t handle;
    TEST_ASSERT_EQUAL(ESP_OK, nvs_open("oc_user", NVS_READWRITE, &handle));
    TEST_ASSERT_EQUAL(ESP_OK, nvs_set_blob(handle, "cfg0", &legacy, sizeof(legacy)));
    TEST_ASSERT_EQUAL(ESP_OK, nvs_set_u8(handle, "active", 0));
    TEST_ASSERT_EQUAL(ESP_OK, nvs_commit(handle));
    nvs_close(handle);

    ConfigLoadResult loaded = UserConfigStore::load();
    TEST_ASSERT_EQUAL(ConfigStatus::MigrationRequired, loaded.status);
    TEST_ASSERT_EQUAL(kUserConfigSchemaVersion, loaded.config.schemaVersion);
    TEST_ASSERT_EQUAL_STRING("", loaded.config.hostname);
    TEST_ASSERT_EQUAL_STRING("Living room", loaded.config.friendlyName);
    TEST_ASSERT_EQUAL_STRING("SRK25ZSX", loaded.config.ac.modelId);
    TEST_ASSERT_TRUE(loaded.config.connectors.mqttEnabled);
    TEST_ASSERT_EQUAL_STRING("broker.local", loaded.config.connectors.mqttHost);

    copyBounded(loaded.config.hostname, sizeof(loaded.config.hostname), "airco-woonkamer");
    TEST_ASSERT_EQUAL(ConfigStatus::Ok, UserConfigStore::save(loaded.config));
    loaded = UserConfigStore::load();
    TEST_ASSERT_EQUAL(ConfigStatus::Ok, loaded.status);
    TEST_ASSERT_EQUAL_STRING("airco-woonkamer", loaded.config.hostname);
    char resolved[kMaxHostnameLen + 1];
    TEST_ASSERT_EQUAL(ESP_OK, resolveDeviceHostname(loaded.config, resolved, sizeof(resolved)));
    TEST_ASSERT_EQUAL_STRING("airco-woonkamer", resolved);
    TEST_ASSERT_EQUAL(ConfigStatus::Ok, UserConfigStore::clear());
    TEST_ASSERT_EQUAL(ConfigStatus::NotFound, UserConfigStore::load().status);
}

}  // namespace opencomfort

#include "src/Logging/LogBuffer.hpp"

namespace opencomfort {

TEST_CASE("ESP_LOG lines are split into level, time, tag and message", "[logs]")
{
    LogEntry entry;
    TEST_ASSERT_TRUE(parseLogLine("\033[0;33mW (12345) HomeAssistantMqtt: Broker closed the connection\033[0m\n", entry));
    TEST_ASSERT_EQUAL('W', entry.level);
    TEST_ASSERT_EQUAL_UINT32(12345, entry.ms);
    TEST_ASSERT_EQUAL_STRING("HomeAssistantMqtt", entry.tag);
    TEST_ASSERT_EQUAL_STRING("Broker closed the connection", entry.message);

    TEST_ASSERT_TRUE(parseLogLine("I (7) Application: Ready: yes\n", entry));  // no colour
    TEST_ASSERT_EQUAL_STRING("Application", entry.tag);
    TEST_ASSERT_EQUAL_STRING("Ready: yes", entry.message);

    TEST_ASSERT_FALSE(parseLogLine("plain printf output\n", entry));
    TEST_ASSERT_FALSE(parseLogLine("I (abc) Tag: text", entry));
    TEST_ASSERT_FALSE(parseLogLine("", entry));
}

}  // namespace opencomfort
