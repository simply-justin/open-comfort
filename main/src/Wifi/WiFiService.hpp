#pragma once

#include <cstddef>
#include <cstdint>

extern "C" {
#include "esp_err.h"
#include "esp_wifi_types.h"
}

namespace opencomfort {

enum class WifiMode : uint8_t {
    Off = 0,
    SoftAp,
    Station,
    ApSta,
};

enum class WifiStaState : uint8_t {
    Idle = 0,
    Connecting,
    Connected,
    Failed,
};

struct WifiCredentials {
    char ssid[33] = {};
    char password[65] = {};
};

struct WifiScanEntry {
    char ssid[33] = {};
    int8_t rssi = 0;
    wifi_auth_mode_t authmode = WIFI_AUTH_OPEN;
};

class WiFiService {
public:
    static esp_err_t init();
    static esp_err_t startSoftAp(const char* ssid, const char* password);
    static esp_err_t stopSoftAp();
    static esp_err_t connectStation(const WifiCredentials& credentials, uint32_t timeoutMs);
    static esp_err_t startStationFromStorage(uint32_t timeoutMs);
    static esp_err_t disconnectStation();
    static esp_err_t scan(WifiScanEntry* out, size_t capacity, size_t& count, uint32_t timeoutMs);
    static WifiStaState stationState();
    static bool isStationConnected();
    static esp_err_t saveCredentials(const WifiCredentials& credentials);
};

}  // namespace opencomfort
