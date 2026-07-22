#include "src/Bus/BusFactory.hpp"

#include <type_traits>
#include <utility>

extern "C" {
#include "esp_log.h"
}

namespace opencomfort {
namespace {
constexpr const char* kTag = "BusFactory";
}

BusStatus BusFactory::merge(const ProductDefinition& product, const ProtocolBusParams& protocol,
                            BusConfigVariant& out)
{
    if (product.board == nullptr) {
        return BusStatus::InvalidArgument;
    }
    if (protocol.kind == BusKind::Unknown || !productAllowsBus(product, protocol.kind)) {
        return BusStatus::Unsupported;
    }

    switch (protocol.kind) {
        case BusKind::Uart:
        case BusKind::Rs485: {
            UartBusConfig cfg;
            cfg.port = product.board->uart.port;
            cfg.txPin = product.board->uart.txPin;
            cfg.rxPin = product.board->uart.rxPin;
            cfg.dePin = product.board->uart.dePin;
            cfg.rs485 = (protocol.kind == BusKind::Rs485);
            cfg.baudRate = protocol.baudOrClockHz > 0 ? protocol.baudOrClockHz : 9600;
            if (cfg.baudRate > product.board->maxUartBaud) {
                return BusStatus::InvalidBaud;
            }
            out = cfg;
            return BusStatus::Ok;
        }
        case BusKind::Spi: {
            SpiBusConfig cfg;
            cfg.host = product.board->spi.host;
            cfg.sckPin = product.board->spi.sckPin;
            cfg.mosiPin = product.board->spi.mosiPin;
            cfg.misoPin = product.board->spi.misoPin;
            cfg.csPin = product.board->spi.csPin;
            cfg.clockHz = protocol.baudOrClockHz > 0 ? protocol.baudOrClockHz : 1000000;
            cfg.mode = protocol.spiMode;
            if (cfg.clockHz > product.board->maxSpiClockHz) {
                return BusStatus::InvalidBaud;
            }
            out = cfg;
            return BusStatus::Ok;
        }
        case BusKind::Can: {
            CanBusConfig cfg;
            cfg.txPin = product.board->can.txPin;
            cfg.rxPin = product.board->can.rxPin;
            cfg.baudRate = protocol.baudOrClockHz > 0 ? protocol.baudOrClockHz : 500000;
            out = cfg;
            return BusStatus::Ok;
        }
        default:
            return BusStatus::Unsupported;
    }
}

BusStatus BusFactory::validateConfig(const BusConfigVariant& cfg)
{
    return std::visit(
        [](const auto& concrete) -> BusStatus {
            return validate(concrete);
        },
        cfg);
}

BusResult BusFactory::createOpen(const BusConfigVariant& cfg, BusHandle& out)
{
    out = BusHandle{};

    return std::visit(
        [&](const auto& concrete) -> BusResult {
            using T = std::decay_t<decltype(concrete)>;

            if constexpr (std::is_same_v<T, UartBusConfig>) {
                auto transport = std::make_unique<UartTransport>(concrete);
                BusResult opened = transport->open();
                if (!opened) {
                    return opened;
                }
                out.kind = concrete.rs485 ? BusKind::Rs485 : BusKind::Uart;
                out.stream = std::move(transport);
                ESP_LOGI(kTag, "Created UART/RS485 transport");
                return BusResult::ok();
            } else if constexpr (std::is_same_v<T, SpiBusConfig>) {
                auto transport = std::make_unique<SpiTransport>(concrete);
                BusResult opened = transport->open();
                if (!opened) {
                    return opened;
                }
                out.kind = BusKind::Spi;
                out.spi = std::move(transport);
                ESP_LOGI(kTag, "Created SPI transport");
                return BusResult::ok();
            } else if constexpr (std::is_same_v<T, CanBusConfig>) {
                auto transport = std::make_unique<CanTransport>(concrete);
                BusResult opened = transport->open();
                if (!opened) {
                    return opened;
                }
                out.kind = BusKind::Can;
                out.can = std::move(transport);
                ESP_LOGI(kTag, "Created CAN transport");
                return BusResult::ok();
            } else {
                return BusResult::fail(BusStatus::Unsupported);
            }
        },
        cfg);
}

}  // namespace opencomfort
