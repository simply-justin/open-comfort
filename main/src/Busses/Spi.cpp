#include "Spi.hpp"

extern "C" {
    #include "driver/spi_master.h"
    #include "esp_log.h"
}

static const char *TAG = "Spi";

Spi::Spi(const SpiConfig& c)
    : cfg(c)
{}

/*
 * Initialize the SPI bus using cfg.
 * Only MOSI/MISO/SCK are configured; quad pins remain unused.
 */
bool Spi::init() {
    spi_bus_config_t buscfg = {
        .mosi_io_num = cfg.mosiPin,
        .miso_io_num = cfg.misoPin,
        .sclk_io_num = cfg.sckPin,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 256 // suitable for small frames
    };

    // Initialize the SPI bus
    if (spi_bus_initialize(cfg.host, &buscfg, SPI_DMA_CH_AUTO) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize SPI bus");
        return false;
    }

    // Device configuration (CS pin + mode + clock speed)
    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = cfg.frequency,
        .mode = cfg.mode,
        .spics_io_num = cfg.csPin,  // Chip select pin
        .queue_size = 1,
    };

    // Attach the SPI device
    if (spi_bus_add_device(cfg.host, &devcfg, &handle) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add SPI device");
        return false;
    }

    ESP_LOGI(TAG, "SPI initialized (host=%d, SCK=%d, MOSI=%d, MISO=%d, CS=%d)",
             cfg.host, cfg.sckPin, cfg.mosiPin, cfg.misoPin, cfg.csPin);

    return true;
}

/*
 * Blocking write transaction.
 */
bool Spi::write(const uint8_t *data, size_t len) {
    if (!handle)
        return false;

    spi_transaction_t t = {};
    t.length = len * 8;
    t.tx_buffer = data;

    esp_err_t err = spi_device_transmit(handle, &t);
    return err == ESP_OK;
}

/*
 * Blocking read transaction (full-duplex using dummy bytes).
 */
int Spi::read(uint8_t *buffer, size_t maxLen) {
    if (!handle)
        return 0;

    spi_transaction_t t = {};
    t.length = maxLen * 8;
    t.rxlength = maxLen * 8;
    t.rx_buffer = buffer;
    t.tx_buffer = nullptr; // driver will send 0x00 filler

    esp_err_t err = spi_device_transmit(handle, &t);
    if (err != ESP_OK)
        return 0;

    return maxLen;
}
