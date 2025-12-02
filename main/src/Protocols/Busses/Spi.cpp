#include "Spi.hpp"
extern "C" {
#include "driver/spi_master.h"
#include "esp_log.h"
}

static const char *TAG = "SpiBus";

bool SpiBus::init(const BusConfig &cfg) {
    spi_bus_config_t buscfg = {
        .mosi_io_num = cfg.mosiPin,
        .miso_io_num = cfg.misoPin,
        .sclk_io_num = cfg.sckPin,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 256
    };

    ESP_ERROR_CHECK(spi_bus_initialize(VSPI_HOST, &buscfg, SPI_DMA_CH_AUTO));
    ESP_LOGI(TAG, "SPI init SCK=%d MOSI=%d MISO=%d", cfg.sckPin, cfg.mosiPin, cfg.misoPin);
    return true;
}

void SpiBus::write(const uint8_t *data, size_t len) {
    // Implement later for AC protocols if needed
}

int SpiBus::read(uint8_t *buffer, size_t maxLen, uint32_t timeoutMs) {
    return 0; // No-op for now
}
