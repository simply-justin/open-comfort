#include "unity.h"
#include "unity_test_runner.h"

#include <cstring>
#include <memory>

#include "src/Modules/Ac/AcProtocolEngine.hpp"

namespace opencomfort {
namespace {

class CaptureSpi final : public ISpiDevice {
public:
    BusResult open() override { return BusResult::ok(0); }
    BusResult close() override { return BusResult::ok(0); }
    bool isOpen() const override { return true; }
    BusResult transfer(const uint8_t* tx, uint8_t*, size_t len, TickType_t) override
    {
        return write(tx, len, 0);
    }
    BusResult write(const uint8_t* data, size_t len, TickType_t) override
    {
        std::memcpy(last, data, len);
        return BusResult::ok(len);
    }
    BusResult read(uint8_t*, size_t, TickType_t) override { return BusResult::ok(0); }
    const char* name() const override { return "capture"; }

    uint8_t last[kMaxAcFrameBytes] = {};
};

constexpr const char* kProfile = R"({
  "frameSize": 4,
  "capabilities": { "minTemp": 16, "maxTemp": 30 },
  "commands": {
    "set_power": [ { "field": "DB0", "binary_or": ["02", "$POWER"] } ],
    "set_mode": [ { "field": "DB3", "binary_or": ["20", "$MODE"] } ],
    "set_temperature": [ { "field": "DB2", "binary_or": ["80", "$TEMP"] } ],
    "set_fan_speed": [ { "field": "DB1", "binary_or": ["08", "$FAN"] } ]
  }
})";

}  // namespace

TEST_CASE("binary_or clears previously set placeholder bits", "[ac]")
{
    AcProtocolEngine engine;
    TEST_ASSERT_EQUAL(ESP_OK, engine.loadFromJson(kProfile));

    BusHandle bus;
    bus.kind = BusKind::Spi;
    auto spi = std::make_unique<CaptureSpi>();
    CaptureSpi* capture = spi.get();
    bus.spi = std::move(spi);

    AcDesiredState on{};
    on.powerOn = true;
    on.mode = AcMode::Fan;  // 0x04
    on.targetTempC = 30.0f;
    on.fan = AcFanSpeed::High;
    TEST_ASSERT_EQUAL(ESP_OK, engine.applyDesiredState(bus, on));
    TEST_ASSERT_EQUAL_HEX8(0x03, capture->last[0]);
    TEST_ASSERT_EQUAL_HEX8(0x24, capture->last[3]);
    TEST_ASSERT_EQUAL_HEX8(0x0B, capture->last[1]);
    TEST_ASSERT_EQUAL_HEX8(0x9E, capture->last[2]);

    AcDesiredState off{};
    off.powerOn = false;
    off.mode = AcMode::Cool;  // 0x01
    off.targetTempC = 17.0f;
    off.fan = AcFanSpeed::Auto;
    TEST_ASSERT_EQUAL(ESP_OK, engine.applyDesiredState(bus, off));
    TEST_ASSERT_EQUAL_HEX8(0x02, capture->last[0]);  // power bit cleared, flag kept
    TEST_ASSERT_EQUAL_HEX8(0x21, capture->last[3]);
    TEST_ASSERT_EQUAL_HEX8(0x08, capture->last[1]);
    TEST_ASSERT_EQUAL_HEX8(0x91, capture->last[2]);
}

}  // namespace opencomfort
