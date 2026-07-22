#pragma once

#include "src/Config/ConnectorSecretStore.hpp"
#include "src/Config/UserConfig.hpp"
#include "src/Setup/SetupSchema.hpp"
#include "src/Wifi/WiFiService.hpp"

namespace opencomfort {

struct SetupDraft {
    WifiCredentials wifi = {};
    bool wifiVerified = false;
    char friendlyName[kMaxFriendlyNameLen + 1] = "OpenComfort";
    char profileId[kMaxProfileIdLen + 1] = {};
    char modelId[kMaxModelIdLen + 1] = {};
    char profileVersion[kMaxProfileVersionLen + 1] = {};
    BusKind bus = BusKind::Unknown;
    ConnectorConfig connectors = {};
    MqttCredentials mqttCredentials = {};
    char adminPassword[65] = {};
};

class ISetupProvider {
public:
    virtual ~ISetupProvider() = default;
    virtual const char* moduleId() const = 0;
    virtual bool buildSchema(SetupSchema& out) = 0;
    virtual bool validateDraft(const SetupDraft& draft, char* error, size_t errorLen) const = 0;
    virtual bool finalize(const SetupDraft& draft, UserConfig& out, char* error,
                          size_t errorLen) const = 0;
};

}  // namespace opencomfort
