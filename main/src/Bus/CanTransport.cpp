#include "src/Bus/CanTransport.hpp"

#include <cstring>

extern "C" {
#include "driver/twai.h"
#include "esp_log.h"
}

namespace opencomfort {
namespace {
constexpr const char* kTag = "CanTransport";
}

CanTransport::CanTransport(CanBusConfig cfg) : cfg_(cfg) {}

CanTransport::~CanTransport()
{
    close();
}

const char* CanTransport::name() const
{
    return "can";
}

bool CanTransport::isOpen() const
{
    return open_;
}

BusResult CanTransport::open()
{
    if (open_) {
        return BusResult::fail(BusStatus::AlreadyOpen);
    }

    const BusStatus status = validate(cfg_);
    if (status != BusStatus::Ok) {
        return BusResult::fail(status);
    }

    twai_general_config_t gConfig = TWAI_GENERAL_CONFIG_DEFAULT(cfg_.txPin, cfg_.rxPin, TWAI_MODE_NORMAL);
    twai_timing_config_t tConfig = TWAI_TIMING_CONFIG_500KBITS();
    switch (cfg_.baudRate) {
        case 125000:
            tConfig = TWAI_TIMING_CONFIG_125KBITS();
            break;
        case 250000:
            tConfig = TWAI_TIMING_CONFIG_250KBITS();
            break;
        case 500000:
            tConfig = TWAI_TIMING_CONFIG_500KBITS();
            break;
        default:
            return BusResult::fail(BusStatus::InvalidBaud);
    }

    twai_filter_config_t fConfig = TWAI_FILTER_CONFIG_ACCEPT_ALL();
    if (cfg_.filterEnabled) {
        fConfig.acceptance_code = (cfg_.rxFilterId << 21);
        fConfig.acceptance_mask = ~(0x7FFU << 21);
        fConfig.single_filter = true;
    }

    if (twai_driver_install(&gConfig, &tConfig, &fConfig) != ESP_OK) {
        return BusResult::fail(BusStatus::DriverInstallFailed);
    }
    if (twai_start() != ESP_OK) {
        twai_driver_uninstall();
        return BusResult::fail(BusStatus::DriverInstallFailed);
    }

    open_ = true;
    ESP_LOGI(kTag, "Opened CAN tx=%d rx=%d baud=%d", cfg_.txPin, cfg_.rxPin, cfg_.baudRate);
    return BusResult::ok();
}

BusResult CanTransport::close()
{
    if (!open_) {
        return BusResult::ok();
    }
    twai_stop();
    twai_driver_uninstall();
    open_ = false;
    return BusResult::ok();
}

BusResult CanTransport::send(const CanFrame& frame, TickType_t timeout)
{
    if (!open_) {
        return BusResult::fail(BusStatus::NotOpen);
    }
    if (frame.dlc > 8) {
        return BusResult::fail(BusStatus::InvalidArgument);
    }

    twai_message_t msg = {};
    msg.identifier = frame.id != 0 ? frame.id : cfg_.txId;
    msg.data_length_code = frame.dlc;
    msg.extd = frame.extended ? 1 : 0;
    msg.rtr = 0;
    std::memcpy(msg.data, frame.data, frame.dlc);

    const esp_err_t err = twai_transmit(&msg, timeout);
    if (err == ESP_ERR_TIMEOUT) {
        return BusResult::fail(BusStatus::Timeout);
    }
    if (err != ESP_OK) {
        return BusResult::fail(BusStatus::IoError);
    }
    return BusResult::ok(frame.dlc);
}

BusResult CanTransport::receive(CanFrame& frame, TickType_t timeout)
{
    if (!open_) {
        return BusResult::fail(BusStatus::NotOpen);
    }

    twai_message_t msg = {};
    const esp_err_t err = twai_receive(&msg, timeout);
    if (err == ESP_ERR_TIMEOUT) {
        return BusResult::fail(BusStatus::Timeout);
    }
    if (err != ESP_OK) {
        return BusResult::fail(BusStatus::IoError);
    }

    frame.id = msg.identifier;
    frame.extended = msg.extd;
    frame.dlc = msg.data_length_code;
    std::memset(frame.data, 0, sizeof(frame.data));
    std::memcpy(frame.data, msg.data, msg.data_length_code);
    return BusResult::ok(frame.dlc);
}

}  // namespace opencomfort
