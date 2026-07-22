#include "src/Bus/UartTransport.hpp"

extern "C" {
#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_log.h"
}

namespace opencomfort {
namespace {
constexpr const char* kTag = "UartTransport";
}

UartTransport::UartTransport(UartBusConfig cfg) : cfg_(cfg) {}

UartTransport::~UartTransport()
{
    close();
}

const char* UartTransport::name() const
{
    return cfg_.rs485 ? "rs485" : "uart";
}

bool UartTransport::isOpen() const
{
    return open_;
}

BusResult UartTransport::open()
{
    if (open_) {
        return BusResult::fail(BusStatus::AlreadyOpen);
    }

    const BusStatus status = validate(cfg_);
    if (status != BusStatus::Ok) {
        return BusResult::fail(status);
    }

    uart_config_t uartCfg = {};
    uartCfg.baud_rate = cfg_.baudRate;
    uartCfg.data_bits = UART_DATA_8_BITS;
    uartCfg.parity = UART_PARITY_DISABLE;
    uartCfg.stop_bits = UART_STOP_BITS_1;
    uartCfg.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    uartCfg.source_clk = UART_SCLK_DEFAULT;

    if (uart_param_config(cfg_.port, &uartCfg) != ESP_OK) {
        return BusResult::fail(BusStatus::DriverInstallFailed);
    }

    if (uart_set_pin(cfg_.port, cfg_.txPin, cfg_.rxPin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE) !=
        ESP_OK) {
        return BusResult::fail(BusStatus::DriverInstallFailed);
    }

    if (uart_driver_install(cfg_.port, 2048, 0, 0, nullptr, 0) != ESP_OK) {
        return BusResult::fail(BusStatus::DriverInstallFailed);
    }

    if (cfg_.rs485 && cfg_.dePin != GPIO_NUM_NC) {
        gpio_config_t io = {};
        io.pin_bit_mask = 1ULL << cfg_.dePin;
        io.mode = GPIO_MODE_OUTPUT;
        io.pull_up_en = GPIO_PULLUP_DISABLE;
        io.pull_down_en = GPIO_PULLDOWN_DISABLE;
        io.intr_type = GPIO_INTR_DISABLE;
        if (gpio_config(&io) != ESP_OK) {
            uart_driver_delete(cfg_.port);
            return BusResult::fail(BusStatus::DriverInstallFailed);
        }
        gpio_set_level(cfg_.dePin, 0);
    }

    open_ = true;
    ESP_LOGI(kTag, "Opened %s port=%d tx=%d rx=%d baud=%d", name(), cfg_.port, cfg_.txPin,
             cfg_.rxPin, cfg_.baudRate);
    return BusResult::ok();
}

BusResult UartTransport::close()
{
    if (!open_) {
        return BusResult::ok();
    }

    uart_driver_delete(cfg_.port);
    open_ = false;
    return BusResult::ok();
}

BusResult UartTransport::write(const uint8_t* data, size_t len)
{
    if (!open_) {
        return BusResult::fail(BusStatus::NotOpen);
    }
    if (data == nullptr || len == 0) {
        return BusResult::fail(BusStatus::InvalidArgument);
    }

    if (cfg_.rs485 && cfg_.dePin != GPIO_NUM_NC) {
        gpio_set_level(cfg_.dePin, 1);
    }

    const int written = uart_write_bytes(cfg_.port, reinterpret_cast<const char*>(data), len);
    if (written < 0 || static_cast<size_t>(written) != len) {
        if (cfg_.rs485 && cfg_.dePin != GPIO_NUM_NC) {
            gpio_set_level(cfg_.dePin, 0);
        }
        return BusResult::fail(BusStatus::IoError);
    }

    if (uart_wait_tx_done(cfg_.port, cfg_.writeTimeout) != ESP_OK) {
        if (cfg_.rs485 && cfg_.dePin != GPIO_NUM_NC) {
            gpio_set_level(cfg_.dePin, 0);
        }
        return BusResult::fail(BusStatus::Timeout);
    }

    if (cfg_.rs485 && cfg_.dePin != GPIO_NUM_NC) {
        gpio_set_level(cfg_.dePin, 0);
    }

    return BusResult::ok(len);
}

BusResult UartTransport::read(uint8_t* buffer, size_t maxLen, TickType_t timeout)
{
    if (!open_) {
        return BusResult::fail(BusStatus::NotOpen);
    }
    if (buffer == nullptr || maxLen == 0) {
        return BusResult::fail(BusStatus::InvalidArgument);
    }

    const int received = uart_read_bytes(cfg_.port, buffer, maxLen, timeout);
    if (received < 0) {
        return BusResult::fail(BusStatus::IoError);
    }
    if (received == 0) {
        return BusResult::fail(BusStatus::Timeout);
    }
    return BusResult::ok(static_cast<size_t>(received));
}

}  // namespace opencomfort
