// SPI frame synchronization adapted from hberntsen/mhi-ac-ctrl-esp32 (MIT, see MhiLink.LICENSE).
#include "src/Modules/Ac/MhiLink.hpp"

#include <cmath>
#include <cstring>
#include "driver/gpio.h"
#include "driver/pulse_cnt.h"
#include "driver/rmt_rx.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "driver/spi_slave.h"
#include "esp_rom_gpio.h"
#include "soc/gpio_sig_map.h"
#include "soc/spi_periph.h"
#include "esp_log.h"

namespace opencomfort::mhi {
namespace {
// Peripheral and queued buffers live for the application lifetime. Only the module task
// queues transfers. The RMT interrupt marks frame boundaries on the internal CS input.
spi_host_device_t host;
int csSignal;
size_t wireSize = frameSize;
rmt_channel_handle_t clockChannel;
rmt_receive_config_t receiveConfig{};
rmt_symbol_word_t symbols[SOC_RMT_MEM_WORDS_PER_CHANNEL];
DMA_ATTR uint8_t tx[36], rx[36];
spi_slave_transaction_t transaction{};
bool queued = false;
bool phase = false;
volatile bool clockFault = false;
portMUX_TYPE selectLock = portMUX_INITIALIZER_UNLOCKED;
bool selectNext = false;
bool armed = false;
int sckPin = -1;
uint32_t clockBursts = 0;

void IRAM_ATTR ready(spi_slave_transaction_t*)
{
    portENTER_CRITICAL_ISR(&selectLock);
    selectNext = armed;
    portEXIT_CRITICAL_ISR(&selectLock);
}


bool IRAM_ATTR frameGap(rmt_channel_handle_t channel, const rmt_rx_done_event_data_t* event, void*)
{
    portENTER_CRITICAL_ISR(&selectLock);
    esp_rom_gpio_connect_in_signal(GPIO_MATRIX_CONST_ONE_INPUT, csSignal, false);
    if (event->num_symbols > 1) ++clockBursts;
    if (selectNext) {
        esp_rom_gpio_connect_in_signal(GPIO_MATRIX_CONST_ZERO_INPUT, csSignal, false);
        selectNext = false;
    }
    portEXIT_CRITICAL_ISR(&selectLock);
    if (rmt_receive(channel, symbols, sizeof(symbols), &receiveConfig) != ESP_OK) clockFault = true;
    return false;
}

uint16_t checksum(const uint8_t* frame, size_t bytes)
{
    uint16_t sum = 0;
    for (size_t i = 0; i < bytes; ++i) sum += frame[i];
    return sum;
}

esp_err_t receive(TickType_t wait, AcReportedState& state)
{
    spi_slave_transaction_t* done = nullptr;
    const esp_err_t err = spi_slave_get_trans_result(host, &done, wait);
    if (err != ESP_OK) return err;
    queued = false;
    if (done != &transaction || transaction.trans_len != wireSize * 8 ||
        !decode(rx, wireSize, state)) return ESP_ERR_INVALID_RESPONSE;
    return ESP_OK;
}

esp_err_t queue(const AcDesiredState* command)
{
    encode(tx, command, phase, wireSize);
    phase = !phase;
    transaction = {};
    transaction.tx_buffer = tx;
    transaction.rx_buffer = rx;
    transaction.length = sizeof(rx) * 8;
    portENTER_CRITICAL(&selectLock);
    armed = true;
    portEXIT_CRITICAL(&selectLock);
    const esp_err_t err = spi_slave_queue_trans(host, &transaction, 0);
    queued = err == ESP_OK;

    return err;
}
}

bool decode(const uint8_t* frame, size_t length, AcReportedState& state)
{
    state = {};
    if ((length != frameSize && length != extendedFrameSize) || (frame[0] & 0xfe) != 0x6c || frame[1] != 0x80 ||
        frame[2] != 4 || checksum(frame, 18) != ((frame[18] << 8) | frame[19])) return false;
    if (length == extendedFrameSize && static_cast<uint8_t>(checksum(frame, 32)) != frame[32]) return false;
    state.returnAirC = (static_cast<int>(frame[6]) - 61) / 4.0f;
    state.setTempC = (frame[5] & 0x7f) / 2.0f;
    state.hasRoomTemp = true;
    state.hasSetTemp = true;
    // DB0 has the same layout in both directions: bit 0 power, bits 2-4 mode (see encode()).
    state.powerOn = (frame[3] & 0x01) != 0;
    state.hasPower = true;
    switch (frame[3] & 0x1c) {
        case 0x00: state.mode = AcMode::Auto; state.hasMode = true; break;
        case 0x04: state.mode = AcMode::Dry; state.hasMode = true; break;
        case 0x08: state.mode = AcMode::Cool; state.hasMode = true; break;
        case 0x0c: state.mode = AcMode::Fan; state.hasMode = true; break;
        case 0x10: state.mode = AcMode::Heat; state.hasMode = true; break;
        default: break;
    }
    state.valid = true;
    return true;
}

void encode(uint8_t* frame, const AcDesiredState* command, bool second, size_t length)
{
    constexpr uint8_t idle[frameSize] = {0xa9,0,7,0,0,0,0xff,0,0,0x80,0,0,0xff,0xff,0xff,0xff,0x0f};
    std::memcpy(frame, idle, frameSize);
    if (length == extendedFrameSize) frame[0] = 0xaa;
    if (command) {
        constexpr uint8_t modes[] = {0,8,16,4,12,0}; // off, cool, heat, dry, fan, auto
        constexpr uint8_t fans[] = {7,1,2,6}; // auto, low, medium, high
        frame[3] = 2 | command->powerOn;
        if (command->mode != AcMode::Off)
            frame[3] |= 0x20 | modes[static_cast<unsigned>(command->mode)];
        frame[4] = 8 | fans[static_cast<unsigned>(command->fan)];
        frame[5] = 0x80 | static_cast<uint8_t>(std::lround(command->targetTempC * 2));
    }
    frame[17] = second ? 4 : 0;
    const uint16_t sum = checksum(frame, 18);
    frame[18] = sum >> 8;
    frame[19] = sum & 0xff;
    if (length == extendedFrameSize) {
        std::memset(frame + 20, 0, 8);
        std::memset(frame + 28, 0xff, 4);
        frame[32] = static_cast<uint8_t>(checksum(frame, 32));
    }
}

esp_err_t start(const BoardSpiPins& pins, bool extended)
{
    wireSize = extended ? extendedFrameSize : frameSize;
    sckPin = pins.sckPin;
    ESP_LOGI("MhiLink", "CNS SCK=%d MOSI=%d MISO=%d frame=%u", pins.sckPin, pins.mosiPin, pins.misoPin, unsigned(wireSize));
    host = pins.host;
    spi_bus_config_t bus{};
    bus.sclk_io_num = pins.sckPin;
    bus.flags = SPICOMMON_BUSFLAG_GPIO_PINS | SPICOMMON_BUSFLAG_SLAVE;
    bus.mosi_io_num = pins.mosiPin;
    bus.miso_io_num = pins.misoPin;
    bus.quadwp_io_num = bus.quadhd_io_num = -1;
    bus.data4_io_num = bus.data5_io_num = bus.data6_io_num = bus.data7_io_num = -1;
    spi_slave_interface_config_t slave{};
    slave.spics_io_num = -1;
    slave.flags = SPI_SLAVE_BIT_LSBFIRST;
    slave.queue_size = 1;
    slave.mode = 3;
    slave.post_setup_cb = ready;
    esp_err_t err = spi_slave_initialize(host, &bus, &slave, SPI_DMA_DISABLED);
    if (err != ESP_OK) return err;
    csSignal = spi_periph_signal[host].spics_in;
    esp_rom_gpio_connect_in_signal(GPIO_MATRIX_CONST_ONE_INPUT, csSignal, false);
    rmt_rx_channel_config_t cfg{};
    cfg.gpio_num = pins.sckPin;
    cfg.clk_src = RMT_CLK_SRC_DEFAULT;
    cfg.resolution_hz = 1000000;
    cfg.mem_block_symbols = SOC_RMT_MEM_WORDS_PER_CHANNEL;
    err = rmt_new_rx_channel(&cfg, &clockChannel);
    rmt_rx_event_callbacks_t callbacks{};
    callbacks.on_recv_done = frameGap;
    if (err == ESP_OK) err = rmt_rx_register_event_callbacks(clockChannel, &callbacks, nullptr);
    if (err == ESP_OK) err = rmt_enable(clockChannel);
    if (err == ESP_OK) {
        receiveConfig.signal_range_max_ns = 5000000; // CNS frame boundary: 5 ms of idle clock
        err = rmt_receive(clockChannel, symbols, sizeof(symbols), &receiveConfig);
        if (err != ESP_OK) ESP_ERROR_CHECK_WITHOUT_ABORT(rmt_disable(clockChannel));
    }
    if (err != ESP_OK) {
        if (clockChannel) ESP_ERROR_CHECK_WITHOUT_ABORT(rmt_del_channel(clockChannel));
        ESP_ERROR_CHECK_WITHOUT_ABORT(spi_slave_free(host));
    }
    return err;
}

namespace {
constexpr const char* kWiringNamespace = "oc_boot";  // survives a factory reset, like the hardware
constexpr const char* kWiringKey = "mhi_wiring";

// Rising edges on `pin` within ~200 ms, read every 10 ms so a fast clock cannot wrap the counter.
int clockEdges(gpio_num_t pin)
{
    pcnt_unit_config_t unitConfig{};
    unitConfig.low_limit = -1;
    unitConfig.high_limit = 32767;
    pcnt_unit_handle_t unit = nullptr;
    if (pcnt_new_unit(&unitConfig, &unit) != ESP_OK) return 0;
    pcnt_chan_config_t channelConfig{};
    channelConfig.edge_gpio_num = pin;
    channelConfig.level_gpio_num = -1;
    pcnt_channel_handle_t channel = nullptr;
    int edges = 0;
    if (pcnt_new_channel(unit, &channelConfig, &channel) == ESP_OK) {
        // A pin that is not wired (floating) must not count as a clock.
        gpio_pulldown_en(pin);
        pcnt_channel_set_edge_action(channel, PCNT_CHANNEL_EDGE_ACTION_INCREASE,
                                     PCNT_CHANNEL_EDGE_ACTION_HOLD);
        if (pcnt_unit_enable(unit) == ESP_OK) {
            pcnt_unit_clear_count(unit);
            pcnt_unit_start(unit);
            for (int i = 0; i < 20 && edges < 1000; ++i) {
                vTaskDelay(pdMS_TO_TICKS(10));
                int count = 0;
                pcnt_unit_get_count(unit, &count);
                pcnt_unit_clear_count(unit);
                edges += count;
            }
            pcnt_unit_stop(unit);
            pcnt_unit_disable(unit);
        }
        pcnt_del_channel(channel);
    }
    pcnt_del_unit(unit);
    gpio_reset_pin(pin);
    return edges;
}
}  // namespace

size_t detectWiring(const MhiWiring* wirings, size_t count)
{
    uint8_t stored = 0;
    nvs_handle_t handle = 0;
    if (nvs_open(kWiringNamespace, NVS_READONLY, &handle) == ESP_OK) {
        (void)nvs_get_u8(handle, kWiringKey, &stored);
        nvs_close(handle);
    }
    if (stored >= count) stored = 0;

    // A clocking indoor unit gives hundreds of edges; noise or crosstalk a few.
    constexpr int kMinEdges = 32;
    for (size_t i = 0; i < count; ++i) {
        const int edges = clockEdges(wirings[i].pins.sckPin);
        ESP_LOGI("MhiLink", "Clock edges on GPIO%d (%s): %d", wirings[i].pins.sckPin, wirings[i].name, edges);
        if (edges < kMinEdges) continue;
        if (i != stored && nvs_open(kWiringNamespace, NVS_READWRITE, &handle) == ESP_OK) {
            if (nvs_set_u8(handle, kWiringKey, static_cast<uint8_t>(i)) == ESP_OK) (void)nvs_commit(handle);
            nvs_close(handle);
        }
        return i;
    }
    ESP_LOGW("MhiLink", "No AC clock found; using %s", wirings[stored].name);
    return stored;
}

int activeSckPin()
{
    return sckPin;
}

uint32_t observedClockBursts()
{
    portENTER_CRITICAL(&selectLock);
    const uint32_t count = clockBursts;
    portEXIT_CRITICAL(&selectLock);
    return count;
}

esp_err_t exchange(const AcDesiredState* command, AcReportedState& state)
{
    state = {};
    if (clockFault) return ESP_FAIL;
    // Synchronize on a valid incoming frame before arming an outgoing command.
    if (!queued) {
        const esp_err_t err = queue(nullptr);
        if (err != ESP_OK) return err;
    }
    const TickType_t timeout = pdMS_TO_TICKS(150);
    esp_err_t err = receive(timeout, state);
    if (err != ESP_OK || !command) return err;
    // The CNS controller sends each setting in two frames with alternating DB14.
    for (unsigned repeat = 0; repeat < 2; ++repeat) {
        err = queue(command);
        if (err != ESP_OK) return err;
        err = receive(timeout, state);
        if (err != ESP_OK) {
            if (queued) {
                // Disarm a timed-out command before the indoor unit can resume clocking.
                portENTER_CRITICAL(&selectLock);
                armed = false;
                selectNext = false;
                esp_rom_gpio_connect_in_signal(GPIO_MATRIX_CONST_ZERO_INPUT, csSignal, false);
                esp_rom_gpio_connect_in_signal(GPIO_MATRIX_CONST_ONE_INPUT, csSignal, false);
                portEXIT_CRITICAL(&selectLock);
            }
            return err;
        }
    }
    return ESP_OK;
}
}
