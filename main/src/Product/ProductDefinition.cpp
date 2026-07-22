#include "src/Product/ProductDefinition.hpp"

#include "sdkconfig.h"

namespace opencomfort {
namespace {

#if CONFIG_OPENCOMFORT_BOARD_REV_A
constexpr BoardDefinition kBoardRevA = {
    .revision = "rev-a",
    .uart =
        {
            .port = UART_NUM_1,
            .txPin = GPIO_NUM_17,
            .rxPin = GPIO_NUM_18,
            .dePin = GPIO_NUM_NC,
        },
    .spi =
        {
            .host = SPI2_HOST,
            .sckPin = GPIO_NUM_12,
            .mosiPin = GPIO_NUM_11,
            .misoPin = GPIO_NUM_13,
            .csPin = GPIO_NUM_10,
        },
    .can =
        {
            .txPin = GPIO_NUM_1,
            .rxPin = GPIO_NUM_2,
        },
    .factoryResetPin = GPIO_NUM_0,
    .maxSpiClockHz = 1000000,
    .maxUartBaud = 115200,
};
#endif

constexpr BusKind kAcAllowedBuses[] = {
    BusKind::Spi,
    BusKind::Uart,
    BusKind::Can,
};

}  // namespace

const ProductDefinition& currentProduct()
{
#if CONFIG_OPENCOMFORT_MODULE_AC
    static constexpr ProductDefinition kProduct = {
        .productId = CONFIG_OPENCOMFORT_PRODUCT_ID,
        .module = ModuleKind::Ac,
        .board = &kBoardRevA,
        .allowedBuses = kAcAllowedBuses,
        .allowedBusCount = sizeof(kAcAllowedBuses) / sizeof(kAcAllowedBuses[0]),
        .portalApPassword = CONFIG_OPENCOMFORT_PORTAL_AP_PASSWORD,
        .profileCatalogUrl = CONFIG_OPENCOMFORT_PROFILE_CATALOG_URL,
        .supportsMatter = CONFIG_OPENCOMFORT_ENABLE_MATTER,
    };
    return kProduct;
#else
#error "No OpenComfort module selected"
#endif
}

bool productAllowsBus(const ProductDefinition& product, BusKind kind)
{
    for (size_t i = 0; i < product.allowedBusCount; ++i) {
        if (product.allowedBuses[i] == kind) {
            return true;
        }
    }
    return false;
}

}  // namespace opencomfort
