#include "src/Bus/SpiTransport.hpp"

#include <cstring>

extern "C" {
#include "esp_log.h"
}

namespace opencomfort {
namespace {
constexpr const char* kTag = "SpiTransport";
}

SpiTransport::SpiTransport(SpiBusConfig cfg) : cfg_(cfg) {}

SpiTransport::~SpiTransport()
{
    close();
}

const char* SpiTransport::name() const
{
    return "spi";
}

bool SpiTransport::isOpen() const
{
    return open_;
}

BusResult SpiTransport::open()
{
    if (open_) {
        return BusResult::fail(BusStatus::AlreadyOpen);
    }

    const BusStatus status = validate(cfg_);
    if (status != BusStatus::Ok) {
        return BusResult::fail(status);
    }

    spi_bus_config_t buscfg = {};
    buscfg.mosi_io_num = cfg_.mosiPin;
    buscfg.miso_io_num = cfg_.misoPin;
    buscfg.sclk_io_num = cfg_.sckPin;
    buscfg.quadwp_io_num = -1;
    buscfg.quadhd_io_num = -1;
    buscfg.max_transfer_sz = static_cast<int>(cfg_.maxTransfer);

    esp_err_t err = spi_bus_initialize(cfg_.host, &buscfg, SPI_DMA_CH_AUTO);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "spi_bus_initialize failed: %s", esp_err_to_name(err));
        return BusResult::fail(BusStatus::DriverInstallFailed);
    }
    busInstalled_ = true;

    spi_device_interface_config_t devcfg = {};
    devcfg.clock_speed_hz = cfg_.clockHz;
    devcfg.mode = cfg_.mode;
    devcfg.spics_io_num = cfg_.csPin;
    devcfg.queue_size = 1;

    err = spi_bus_add_device(cfg_.host, &devcfg, &handle_);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "spi_bus_add_device failed: %s", esp_err_to_name(err));
        spi_bus_free(cfg_.host);
        busInstalled_ = false;
        handle_ = nullptr;
        return BusResult::fail(BusStatus::DriverInstallFailed);
    }

    open_ = true;
    ESP_LOGI(kTag, "Opened SPI host=%d sck=%d mosi=%d miso=%d cs=%d hz=%d mode=%u", cfg_.host,
             cfg_.sckPin, cfg_.mosiPin, cfg_.misoPin, cfg_.csPin, cfg_.clockHz, cfg_.mode);
    return BusResult::ok();
}

BusResult SpiTransport::close()
{
    if (!open_ && !busInstalled_ && handle_ == nullptr) {
        return BusResult::ok();
    }

    if (handle_ != nullptr) {
        spi_bus_remove_device(handle_);
        handle_ = nullptr;
    }
    if (busInstalled_) {
        spi_bus_free(cfg_.host);
        busInstalled_ = false;
    }
    open_ = false;
    return BusResult::ok();
}

BusResult SpiTransport::transfer(const uint8_t* tx, uint8_t* rx, size_t len, TickType_t /*timeout*/)
{
    if (!open_ || handle_ == nullptr) {
        return BusResult::fail(BusStatus::NotOpen);
    }
    if ((tx == nullptr && rx == nullptr) || len == 0 || len > cfg_.maxTransfer) {
        return BusResult::fail(BusStatus::InvalidArgument);
    }

    spi_transaction_t transaction = {};
    transaction.length = len * 8;
    transaction.tx_buffer = tx;
    transaction.rx_buffer = rx;

    const esp_err_t err = spi_device_transmit(handle_, &transaction);
    if (err != ESP_OK) {
        return BusResult::fail(BusStatus::IoError);
    }
    return BusResult::ok(len);
}

BusResult SpiTransport::write(const uint8_t* data, size_t len, TickType_t timeout)
{
    return transfer(data, nullptr, len, timeout);
}

BusResult SpiTransport::read(uint8_t* buffer, size_t maxLen, TickType_t timeout)
{
    if (buffer == nullptr || maxLen == 0) {
        return BusResult::fail(BusStatus::InvalidArgument);
    }
    std::memset(buffer, 0, maxLen);
    return transfer(nullptr, buffer, maxLen, timeout);
}

}  // namespace opencomfort
