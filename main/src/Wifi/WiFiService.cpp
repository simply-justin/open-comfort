#include "src/Wifi/WiFiService.hpp"

#include <cstring>

extern "C" {
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "nvs_flash.h"
}

namespace opencomfort {
namespace {

constexpr const char* kTag = "WiFiService";
constexpr int kConnectedBit = BIT0;
constexpr int kFailBit = BIT1;

bool s_initialized = false;
EventGroupHandle_t s_events = nullptr;
WifiStaState s_staState = WifiStaState::Idle;
esp_netif_t* s_apNetif = nullptr;
esp_netif_t* s_staNetif = nullptr;
// Set once the device runs from stored credentials; then a lost connection is retried forever.
bool s_autoReconnect = false;

// The STA_START handler may already have started connecting (timing depends on what else
// runs, e.g. Matter and BLE), so "already connecting" is success, not an error.
esp_err_t connectOnce()
{
    const esp_err_t err = esp_wifi_connect();
    return err == ESP_ERR_WIFI_CONN ? ESP_OK : err;
}

// Open networks need an open threshold; otherwise refuse anything weaker than WPA2, so a
// look-alike open access point with the same name is never joined.
wifi_auth_mode_t authThreshold(const char* password)
{
    return (password == nullptr || password[0] == '\0') ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;
}

void onWifiEvent(void* /*arg*/, esp_event_base_t eventBase, int32_t eventId, void* eventData)
{
    if (eventBase == WIFI_EVENT && eventId == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
        return;
    }

    if (eventBase == WIFI_EVENT && eventId == WIFI_EVENT_STA_DISCONNECTED) {
        s_staState = WifiStaState::Failed;
        if (s_autoReconnect) {
            // The driver's own scan/auth timeouts pace these retries (a few seconds each).
            s_staState = WifiStaState::Connecting;
            esp_wifi_connect();
        }
        if (s_events != nullptr) {
            xEventGroupSetBits(s_events, kFailBit);
        }
        return;
    }

    if (eventBase == IP_EVENT && eventId == IP_EVENT_STA_GOT_IP) {
        s_staState = WifiStaState::Connected;
        if (s_events != nullptr) {
            xEventGroupSetBits(s_events, kConnectedBit);
        }
        return;
    }
}

}  // namespace

esp_err_t WiFiService::init()
{
    if (s_initialized) {
        return ESP_OK;
    }

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(nvs_flash_erase());
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        return err;
    }

    err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    s_events = xEventGroupCreate();
    if (s_events == nullptr) {
        return ESP_ERR_NO_MEM;
    }

    s_apNetif = esp_netif_create_default_wifi_ap();
    s_staNetif = esp_netif_create_default_wifi_sta();
    if (s_apNetif == nullptr || s_staNetif == nullptr) {
        return ESP_ERR_NO_MEM;
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&cfg);
    if (err != ESP_OK) {
        return err;
    }

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &onWifiEvent, nullptr));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &onWifiEvent, nullptr));

    s_initialized = true;
    return ESP_OK;
}

esp_err_t WiFiService::setStationHostname(const char* hostname)
{
    if (!s_initialized || s_staNetif == nullptr) return ESP_ERR_INVALID_STATE;
    return esp_netif_set_hostname(s_staNetif, hostname);
}

esp_err_t WiFiService::startSoftAp(const char* ssid, const char* password)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (ssid == nullptr || ssid[0] == '\0' || password == nullptr || std::strlen(password) < 8) {
        return ESP_ERR_INVALID_ARG;
    }

    wifi_config_t ap = {};
    std::strncpy(reinterpret_cast<char*>(ap.ap.ssid), ssid, sizeof(ap.ap.ssid) - 1);
    std::strncpy(reinterpret_cast<char*>(ap.ap.password), password, sizeof(ap.ap.password) - 1);
    ap.ap.ssid_len = static_cast<uint8_t>(std::strlen(reinterpret_cast<char*>(ap.ap.ssid)));
    ap.ap.channel = 1;
    ap.ap.max_connection = 4;
    ap.ap.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(kTag, "SoftAP started ssid=%s", ssid);
    return ESP_OK;
}

esp_err_t WiFiService::stopSoftAp()
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    // Keep STA if connected; otherwise stop radio.
    if (isStationConnected()) {
        return esp_wifi_set_mode(WIFI_MODE_STA);
    }
    esp_wifi_stop();
    return ESP_OK;
}

esp_err_t WiFiService::connectStation(const WifiCredentials& credentials, uint32_t timeoutMs)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (credentials.ssid[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    wifi_config_t sta = {};
    std::strncpy(reinterpret_cast<char*>(sta.sta.ssid), credentials.ssid, sizeof(sta.sta.ssid) - 1);
    std::strncpy(reinterpret_cast<char*>(sta.sta.password), credentials.password,
                 sizeof(sta.sta.password) - 1);
    sta.sta.threshold.authmode = authThreshold(credentials.password);

    xEventGroupClearBits(s_events, kConnectedBit | kFailBit);
    s_staState = WifiStaState::Connecting;

    wifi_mode_t mode = WIFI_MODE_STA;
    esp_wifi_get_mode(&mode);
    // Called from the setup page, possibly twice: report failures instead of aborting.
    esp_err_t err = esp_wifi_set_mode((mode == WIFI_MODE_AP || mode == WIFI_MODE_APSTA)
                                          ? WIFI_MODE_APSTA
                                          : WIFI_MODE_STA);
    if (err == ESP_OK) err = esp_wifi_set_config(WIFI_IF_STA, &sta);
    if (err == ESP_OK) err = esp_wifi_start();
    if (err == ESP_OK) err = connectOnce();
    if (err != ESP_OK) {
        s_staState = WifiStaState::Failed;
        return err;
    }

    const EventBits_t bits =
        xEventGroupWaitBits(s_events, kConnectedBit | kFailBit, pdTRUE, pdFALSE,
                            pdMS_TO_TICKS(timeoutMs));
    if (bits & kConnectedBit) {
        ESP_LOGI(kTag, "Station connected");
        return ESP_OK;
    }

    ESP_LOGW(kTag, "Station connection failed or timed out");
    s_staState = WifiStaState::Failed;
    return ESP_ERR_TIMEOUT;
}

esp_err_t WiFiService::startStationFromStorage(uint32_t timeoutMs)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    xEventGroupClearBits(s_events, kConnectedBit | kFailBit);
    s_staState = WifiStaState::Connecting;
    s_autoReconnect = true;
    esp_err_t err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err == ESP_OK) err = esp_wifi_set_storage(WIFI_STORAGE_FLASH);
    if (err == ESP_OK) err = esp_wifi_start();
    if (err == ESP_OK) err = connectOnce();
    if (err != ESP_OK) {
        s_staState = WifiStaState::Failed;
        return err;
    }

    const EventBits_t bits =
        xEventGroupWaitBits(s_events, kConnectedBit | kFailBit, pdTRUE, pdFALSE,
                            pdMS_TO_TICKS(timeoutMs));
    if (bits & kConnectedBit) {
        return ESP_OK;
    }
    s_staState = WifiStaState::Failed;
    return ESP_ERR_TIMEOUT;
}

esp_err_t WiFiService::disconnectStation()
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    s_autoReconnect = false;
    s_staState = WifiStaState::Idle;
    return esp_wifi_disconnect();
}

esp_err_t WiFiService::scan(WifiScanEntry* out, size_t capacity, size_t& count, uint32_t timeoutMs)
{
    count = 0;
    if (!s_initialized || out == nullptr || capacity == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    wifi_mode_t mode = WIFI_MODE_NULL;
    esp_wifi_get_mode(&mode);
    esp_err_t err = ESP_OK;
    if (mode == WIFI_MODE_NULL) {
        err = esp_wifi_set_mode(WIFI_MODE_APSTA);
        if (err == ESP_OK) err = esp_wifi_start();
        if (err != ESP_OK) return err;
    }

    wifi_scan_config_t scanCfg = {};
    scanCfg.show_hidden = false;
    scanCfg.scan_type = WIFI_SCAN_TYPE_ACTIVE;
    // A scan can fail while the station is connecting; report it instead of aborting.
    err = esp_wifi_scan_start(&scanCfg, true);
    if (err != ESP_OK) return err;

    uint16_t apCount = 0;
    err = esp_wifi_scan_get_ap_num(&apCount);
    if (err != ESP_OK) return err;
    if (apCount == 0) {
        return ESP_OK;
    }

    wifi_ap_record_t* records = static_cast<wifi_ap_record_t*>(calloc(apCount, sizeof(wifi_ap_record_t)));
    if (records == nullptr) {
        return ESP_ERR_NO_MEM;
    }

    err = esp_wifi_scan_get_ap_records(&apCount, records);
    if (err != ESP_OK) {
        free(records);
        return err;
    }
    const size_t limit = (apCount < capacity) ? apCount : capacity;
    for (size_t i = 0; i < limit; ++i) {
        std::strncpy(out[i].ssid, reinterpret_cast<const char*>(records[i].ssid),
                     sizeof(out[i].ssid) - 1);
        out[i].rssi = records[i].rssi;
        out[i].authmode = records[i].authmode;
    }
    count = limit;
    free(records);
    (void)timeoutMs;
    return ESP_OK;
}

WifiStaState WiFiService::stationState()
{
    return s_staState;
}

bool WiFiService::isStationConnected()
{
    return s_staState == WifiStaState::Connected;
}

esp_err_t WiFiService::saveCredentials(const WifiCredentials& credentials)
{
    // Persist via ESP-IDF Wi-Fi storage so passwords stay in NVS.
    wifi_config_t sta = {};
    std::strncpy(reinterpret_cast<char*>(sta.sta.ssid), credentials.ssid, sizeof(sta.sta.ssid) - 1);
    std::strncpy(reinterpret_cast<char*>(sta.sta.password), credentials.password,
                 sizeof(sta.sta.password) - 1);
    sta.sta.threshold.authmode = authThreshold(credentials.password);
    const esp_err_t err = esp_wifi_set_storage(WIFI_STORAGE_FLASH);
    if (err != ESP_OK) {
        return err;
    }
    return esp_wifi_set_config(WIFI_IF_STA, &sta);
}

}  // namespace opencomfort
