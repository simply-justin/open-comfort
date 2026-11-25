#include "unity.h"
#include "unity_test_runner.h"

#include <cstring>
#include "src/Modules/Ac/MhiLink.hpp"
#include <memory>

#include "src/Modules/Ac/AcProtocolEngine.hpp"

namespace opencomfort {
namespace {

class CaptureSpi final : public ISpiDevice {
public:
    BusResult open() override { return BusResult::ok(0); }
    BusResult close() override { return BusResult::ok(0); }
    bool isOpen() const override { return true; }
    BusResult transfer(const uint8_t* tx, uint8_t* rx, size_t len, TickType_t) override
    {
        if (rx != nullptr) std::memset(rx, reply, len);
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
    uint8_t reply = 0xFF;
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

TEST_CASE("an idle bus is not reported as a temperature", "[ac]")
{
    constexpr const char* kPollProfile = R"({
      "frameSize": 4,
      "capabilities": { "minTemp": 16, "maxTemp": 30 },
      "data": [ { "name": "RETURN-AIR", "bytes": ["C0", "80"] } ],
      "commands": { "set_power": [ { "field": "DB0", "binary_or": ["02", "$POWER"] } ] }
    })";
    AcProtocolEngine engine;
    TEST_ASSERT_EQUAL(ESP_OK, engine.loadFromJson(kPollProfile));
    BusHandle bus;
    bus.kind = BusKind::Spi;
    auto spi = std::make_unique<CaptureSpi>();
    CaptureSpi* capture = spi.get();
    bus.spi = std::move(spi);

    AcReportedState reported;
    capture->reply = 0xFF;  // floating MISO
    TEST_ASSERT_EQUAL(ESP_OK, engine.poll(bus, reported));
    TEST_ASSERT_FALSE(reported.valid);
    capture->reply = 0x00;
    TEST_ASSERT_EQUAL(ESP_OK, engine.poll(bus, reported));
    TEST_ASSERT_FALSE(reported.valid);
    capture->reply = 23;
    TEST_ASSERT_EQUAL(ESP_OK, engine.poll(bus, reported));
    TEST_ASSERT_TRUE(reported.valid);
    TEST_ASSERT_EQUAL_FLOAT(23.0f, reported.returnAirC);
}

TEST_CASE("MHI validates short frames and preserves power when setting mode", "[ac]")
{
    uint8_t frame[mhi::frameSize] = {0x6c,0x80,4,0,0,44,149};
    frame[18] = 1;
    frame[19] = 0xb1; // sum of bytes 0..17
    AcReportedState state;
    TEST_ASSERT_TRUE(mhi::decode(frame, sizeof(frame), state));
    TEST_ASSERT_EQUAL_FLOAT(22, state.returnAirC);
    TEST_ASSERT_EQUAL_FLOAT(22, state.setTempC);
    frame[19]++;
    TEST_ASSERT_FALSE(mhi::decode(frame, sizeof(frame), state));
    TEST_ASSERT_FALSE(mhi::decode(frame, 8, state));
    AcDesiredState desired{};
    desired.powerOn = true;
    desired.mode = AcMode::Heat;
    mhi::encode(frame, &desired, false);
    TEST_ASSERT_EQUAL_HEX8(0x33, frame[3]);
    TEST_ASSERT_EQUAL_HEX8(0xac, frame[5]);
    uint16_t sum = 0;
    for (unsigned i = 0; i < 18; ++i) sum += frame[i];
    TEST_ASSERT_EQUAL_UINT16(sum, (frame[18] << 8) | frame[19]);
}

TEST_CASE("MHI extended frames use both checksums and the ESPHome control encoding", "[ac]")
{
    uint8_t frame[33] = {0x6d,0x80,4,0,0,44,149};
    frame[18] = 1;
    frame[19] = 0xb2;
    frame[30] = 1;
    frame[32] = 0x66;
    AcReportedState state;
    TEST_ASSERT_TRUE(mhi::decode(frame, sizeof(frame), state));
    TEST_ASSERT_EQUAL_FLOAT(22, state.returnAirC);
    frame[30] ^= 1;
    TEST_ASSERT_FALSE(mhi::decode(frame, sizeof(frame), state));
    TEST_ASSERT_FALSE(state.valid);
    AcDesiredState desired{};
    desired.powerOn = true;
    desired.mode = AcMode::Heat;
    desired.fan = AcFanSpeed::High;
    desired.targetTempC = 21.5f;
    mhi::encode(frame, &desired, true, sizeof(frame));
    // Reference vector: extended signature, heat/on, high fan, 21.5 C, second checksum.
    const uint8_t expected[] = {
        0xaa,0,7,0x33,0x0e,0xab,0xff,0,0,0x80,0,0,0xff,0xff,0xff,0xff,15,4,
        0x7,0x2b,0,0,0,0,0,0,0,0,0xff,0xff,0xff,0xff,0x59
    };
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, frame, sizeof(frame));
}

namespace {
std::unique_ptr<CaptureSpi> attach(BusHandle& bus)
{
    bus.kind = BusKind::Spi;
    auto spi = std::make_unique<CaptureSpi>();
    return spi;
}
}  // namespace

TEST_CASE("vane positions are encoded and replace each other", "[ac]")
{
    constexpr const char* kProfile = R"({
      "frameSize": 4,
      "capabilities": { "minTemp": 16, "maxTemp": 30,
        "vanes": [ { "name": "auto", "code": "00" }, { "name": "swing", "code": "0A" }, { "name": "1", "code": "02" } ] },
      "commands": {
        "set_power": [ { "field": "DB0", "binary_or": ["02", "$POWER"] } ],
        "set_vanes": [ { "field": "DB1", "binary_or": ["80", "$VANES"] } ]
      }
    })";
    AcProtocolEngine engine;
    TEST_ASSERT_EQUAL(ESP_OK, engine.loadFromJson(kProfile));
    TEST_ASSERT_EQUAL(3, engine.profile().vaneCount);
    TEST_ASSERT_EQUAL_STRING("swing", engine.profile().vaneNames[1]);

    BusHandle bus;
    auto spi = attach(bus);
    CaptureSpi* capture = spi.get();
    bus.spi = std::move(spi);

    AcDesiredState state{};
    state.vanes = 1;  // swing
    TEST_ASSERT_EQUAL(ESP_OK, engine.applyDesiredState(bus, state));
    TEST_ASSERT_EQUAL_HEX8(0x8A, capture->last[1]);
    state.vanes = 2;  // position 1: the swing bits must be cleared
    TEST_ASSERT_EQUAL(ESP_OK, engine.applyDesiredState(bus, state));
    TEST_ASSERT_EQUAL_HEX8(0x82, capture->last[1]);
}

TEST_CASE("read-back registers decode mode, compressor and scaled temperature", "[ac]")
{
    BusHandle bus;
    auto spi = attach(bus);
    CaptureSpi* capture = spi.get();
    bus.spi = std::move(spi);
    AcReportedState reported;

    AcProtocolEngine mode;
    TEST_ASSERT_EQUAL(ESP_OK, mode.loadFromJson(R"({
      "frameSize": 4, "capabilities": { "minTemp": 16, "maxTemp": 30 },
      "data": [ { "name": "MODE", "bytes": ["C0","02"], "reports": "mode", "mask": "1C",
                  "map": { "00": "auto", "04": "dry", "08": "cool", "10": "heat" } } ],
      "commands": { "set_power": [ { "field": "DB0", "binary_or": ["02", "$POWER"] } ] } })"));
    capture->reply = 0x09;  // 0x08 after the mask
    TEST_ASSERT_EQUAL(ESP_OK, mode.poll(bus, reported));
    TEST_ASSERT_TRUE(reported.hasMode);
    TEST_ASSERT_EQUAL(static_cast<int>(AcMode::Cool), static_cast<int>(reported.mode));
    capture->reply = 0x0C;  // not in the map: no mode
    TEST_ASSERT_EQUAL(ESP_OK, mode.poll(bus, reported));
    TEST_ASSERT_FALSE(reported.hasMode);

    AcProtocolEngine room;
    TEST_ASSERT_EQUAL(ESP_OK, room.loadFromJson(R"({
      "frameSize": 4, "capabilities": { "minTemp": 16, "maxTemp": 30 },
      "data": [ { "name": "ROOM", "bytes": ["C0","80"], "reports": "roomTemp", "scale": 0.25, "offset": -15.25 },
                { "name": "COMP", "bytes": ["C0","90"], "reports": "compressor" } ],
      "commands": { "set_power": [ { "field": "DB0", "binary_or": ["02", "$POWER"] } ] } })"));
    capture->reply = 157;  // 157 * 0.25 - 15.25 = 24.0
    TEST_ASSERT_EQUAL(ESP_OK, room.poll(bus, reported));
    TEST_ASSERT_TRUE(reported.hasRoomTemp);
    TEST_ASSERT_EQUAL_FLOAT(24.0f, reported.returnAirC);
    TEST_ASSERT_TRUE(reported.hasCompressor);
    TEST_ASSERT_TRUE(reported.compressorRunning);
}

TEST_CASE("the live action follows power, mode and the compressor", "[ac]")
{
    AcDesiredState desired{};
    desired.powerOn = true;
    desired.mode = AcMode::Cool;
    desired.targetTempC = 21.0f;
    AcReportedState reported{};
    TEST_ASSERT_EQUAL(static_cast<int>(AcAction::Unknown), static_cast<int>(acAction(desired, reported)));

    reported.hasCompressor = true;
    reported.compressorRunning = true;
    TEST_ASSERT_EQUAL(static_cast<int>(AcAction::Cooling), static_cast<int>(acAction(desired, reported)));
    reported.compressorRunning = false;
    TEST_ASSERT_EQUAL(static_cast<int>(AcAction::Idle), static_cast<int>(acAction(desired, reported)));
    reported.hasPower = true;
    reported.powerOn = false;  // the unit says it is off, whatever was asked
    TEST_ASSERT_EQUAL(static_cast<int>(AcAction::Off), static_cast<int>(acAction(desired, reported)));

    reported.powerOn = true;
    reported.compressorRunning = true;
    reported.hasMode = true;
    reported.mode = AcMode::Auto;
    reported.hasRoomTemp = true;
    reported.returnAirC = 18.0f;  // below the target: heating
    TEST_ASSERT_EQUAL(static_cast<int>(AcAction::Heating), static_cast<int>(acAction(desired, reported)));
}

TEST_CASE("MHI frames report power, mode and both temperatures", "[ac]")
{
    uint8_t frame[mhi::frameSize] = {0x6c, 0x80, 4, 0x09 /* on, cool */, 0, 44, 149};
    uint16_t sum = 0;
    for (size_t i = 0; i < 18; ++i) sum += frame[i];
    frame[18] = static_cast<uint8_t>(sum >> 8);
    frame[19] = static_cast<uint8_t>(sum & 0xff);
    AcReportedState state;
    TEST_ASSERT_TRUE(mhi::decode(frame, sizeof(frame), state));
    TEST_ASSERT_TRUE(state.hasRoomTemp && state.hasSetTemp && state.hasPower && state.hasMode);
    TEST_ASSERT_TRUE(state.powerOn);
    TEST_ASSERT_EQUAL(static_cast<int>(AcMode::Cool), static_cast<int>(state.mode));
    TEST_ASSERT_FALSE(state.hasCompressor);  // not in the base frame
}

}  // namespace opencomfort
