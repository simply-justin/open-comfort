#pragma once

#include <string>

class WifiManager {
public:
    static bool init();
    static bool startSetupAp(const std::string& ssid, const std::string& password = "");
    static bool connectStation(const std::string& ssid, const std::string& password, int timeoutMs);
    static bool isStationConnected();
    static void stop();
};
