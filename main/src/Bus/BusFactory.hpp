#pragma once

#include <memory>
#include <variant>

#include "src/Bus/BusConfig.hpp"
#include "src/Bus/BusKind.hpp"
#include "src/Bus/CanTransport.hpp"
#include "src/Bus/IByteStream.hpp"
#include "src/Bus/ICanChannel.hpp"
#include "src/Bus/ISpiDevice.hpp"
#include "src/Bus/SpiTransport.hpp"
#include "src/Bus/UartTransport.hpp"
#include "src/Product/ProductDefinition.hpp"

namespace opencomfort {

struct ProtocolBusParams {
    BusKind kind = BusKind::Unknown;
    int baudOrClockHz = 0;
    uint8_t spiMode = 0;
};

using BusConfigVariant = std::variant<UartBusConfig, SpiBusConfig, CanBusConfig>;

struct BusHandle {
    BusKind kind = BusKind::Unknown;
    std::unique_ptr<IByteStream> stream;
    std::unique_ptr<ISpiDevice> spi;
    std::unique_ptr<ICanChannel> can;
};

class BusFactory {
public:
    static BusStatus merge(const ProductDefinition& product, const ProtocolBusParams& protocol,
                           BusConfigVariant& out);

    static BusStatus validateConfig(const BusConfigVariant& cfg);

    // Creates and opens the transport. Ownership is returned to the caller.
    static BusResult createOpen(const BusConfigVariant& cfg, BusHandle& out);
};

}  // namespace opencomfort
