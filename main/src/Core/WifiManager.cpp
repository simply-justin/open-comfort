#include "src/Core/WifiManager.hpp"

extern "C" {
    #include "esp_event.h"
    #include "esp_log.h"
    #include "esp_netif.h"
    #include "esp_wifi.h"
    #include "freertos/FreeRTOS.h"
    #include "freertos/event_groups.h"
}

#include <cstring>

static const char* TAG = "WifiManager";
static constexpr EventBits_t kConnectedBit = BIT0;
static constexpr EventBits_t kFailedBit = BIT1;

static bool s_initialized = false;
static bool s_started = false;
static bool s_apActive = false;
static EventGroupHandle_t s_wifiEvents = nullptr;

static void wifiEventHandler(void*, esp_event_base_t eventBase, int32_t eventId, void* eventData) {
    if (eventBase == WIFI_EVENT && eventId == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(s_wifiEvents, kConnectedBit);
        xEventGroupSetBits(s_wifiEvents, kFailedBit);
    } else if (eventBase == IP_EVENT && eventId == IP_EVENT_STA_GOT_IP) {
        xEventGroupClearBits(s_wifiEvents, kFailedBit);
        xEventGroupSetBits(s_wifiEvents, kConnectedBit);
    }
}

bool WifiManager::init() {
    if (s_initialized) return true;

    esp_err_t err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "esp_netif_init failed: %s", esp_err_to_name(err));
        return false;
    }

    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "esp_event_loop_create_default failed: %s", esp_err_to_name(err));
        return false;
    }

    esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_init failed: %s", esp_err_to_name(err));
        return false;
    }

    s_wifiEvents = xEventGroupCreate();
    if (!s_wifiEvents) return false;

    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifiEventHandler, nullptr, nullptr);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifiEventHandler, nullptr, nullptr);

    s_initialized = true;
    return true;
}

bool WifiManager::startSetupAp(const std::string& ssid, const std::string& password) {
    if (!init()) return false;

    wifi_config_t apConfig = {};
    std::strncpy(reinterpret_cast<char*>(apConfig.ap.ssid), ssid.c_str(), sizeof(apConfig.ap.ssid));
    apConfig.ap.ssid_len = ssid.size();
    apConfig.ap.channel = 1;
    apConfig.ap.max_connection = 4;
    apConfig.ap.authmode = password.empty() ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA_WPA2_PSK;
    if (!password.empty()) {
        std::strncpy(reinterpret_cast<char*>(apConfig.ap.password), password.c_str(), sizeof(apConfig.ap.password));
    }

    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_set_mode(WIFI_MODE_APSTA));
    esp_err_t err = esp_wifi_set_config(WIFI_IF_AP, &apConfig);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_set_config AP failed: %s", esp_err_to_name(err));
        return false;
    }

    if (!s_started) {
        err = esp_wifi_start();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_wifi_start failed: %s", esp_err_to_name(err));
            return false;
        }
        s_started = true;
    }

    s_apActive = true;
    ESP_LOGI(TAG, "Setup AP started: %s", ssid.c_str());
    return true;
}

bool WifiManager::connectStation(const std::string& ssid, const std::string& password, int timeoutMs) {
    if (!init()) return false;

    wifi_config_t staConfig = {};
    std::strncpy(reinterpret_cast<char*>(staConfig.sta.ssid), ssid.c_str(), sizeof(staConfig.sta.ssid));
    std::strncpy(reinterpret_cast<char*>(staConfig.sta.password), password.c_str(), sizeof(staConfig.sta.password));

    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_set_mode(s_apActive ? WIFI_MODE_APSTA : WIFI_MODE_STA));
    esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &staConfig);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_set_config STA failed: %s", esp_err_to_name(err));
        return false;
    }

    if (!s_started) {
        err = esp_wifi_start();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_wifi_start failed: %s", esp_err_to_name(err));
            return false;
        }
        s_started = true;
    }

    xEventGroupClearBits(s_wifiEvents, kConnectedBit | kFailedBit);
    esp_wifi_connect();

    EventBits_t bits = xEventGroupWaitBits(
        s_wifiEvents,
        kConnectedBit,
        pdFALSE,
        pdFALSE,
        pdMS_TO_TICKS(timeoutMs)
    );

    return (bits & kConnectedBit) != 0;
}

bool WifiManager::isStationConnected() {
    return s_wifiEvents && (xEventGroupGetBits(s_wifiEvents) & kConnectedBit);
}

void WifiManager::stop() {
    if (!s_initialized || !s_started) return;
    esp_wifi_stop();
    s_started = false;
    s_apActive = false;
}
