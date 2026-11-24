#include "src/Modules/Ac/AcProtocolEngine.hpp"

#include <cstdio>
#include <cstring>

extern "C" {
#include "cJSON.h"
#include "esp_log.h"
}

namespace opencomfort {
namespace {

constexpr const char* kTag = "AcProtocol";

bool parseHexByte(const char* text, uint8_t& out)
{
    if (text == nullptr || std::strlen(text) == 0) {
        return false;
    }
    unsigned value = 0;
    if (std::sscanf(text, "%x", &value) != 1 || value > 0xFF) {
        return false;
    }
    out = static_cast<uint8_t>(value);
    return true;
}

bool parseFieldIndex(const char* field, uint8_t& out)
{
    if (field == nullptr || field[0] != 'D' || field[1] != 'B') {
        return false;
    }
    int index = 0;
    if (std::sscanf(field + 2, "%d", &index) != 1 || index < 0 ||
        index >= static_cast<int>(kMaxAcFrameBytes)) {
        return false;
    }
    out = static_cast<uint8_t>(index);
    return true;
}

AcPlaceholder placeholderFromToken(const char* token)
{
    if (token == nullptr) {
        return AcPlaceholder::None;
    }
    if (std::strcmp(token, "$POWER") == 0) {
        return AcPlaceholder::Power;
    }
    if (std::strcmp(token, "$MODE") == 0) {
        return AcPlaceholder::Mode;
    }
    if (std::strcmp(token, "$TEMP") == 0) {
        return AcPlaceholder::Temp;
    }
    if (std::strcmp(token, "$FAN") == 0) {
        return AcPlaceholder::Fan;
    }
    return AcPlaceholder::Literal;
}

}  // namespace

esp_err_t AcProtocolEngine::loadFromJson(const char* jsonText)
{
    profile_ = AcProtocolProfile{};
    if (jsonText == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    cJSON* root = cJSON_Parse(jsonText);
    if (root == nullptr) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    const cJSON* capabilities = cJSON_GetObjectItem(root, "capabilities");
    if (cJSON_IsObject(capabilities)) {
        const cJSON* minTemp = cJSON_GetObjectItem(capabilities, "minTemp");
        const cJSON* maxTemp = cJSON_GetObjectItem(capabilities, "maxTemp");
        if (cJSON_IsNumber(minTemp)) {
            profile_.minTempC = minTemp->valueint;
        }
        if (cJSON_IsNumber(maxTemp)) {
            profile_.maxTempC = maxTemp->valueint;
        }
    }

    const cJSON* frameSize = cJSON_GetObjectItem(root, "frameSize");
    if (cJSON_IsNumber(frameSize) && frameSize->valueint > 0 &&
        frameSize->valueint <= static_cast<int>(kMaxAcFrameBytes)) {
        profile_.frameSize = static_cast<size_t>(frameSize->valueint);
    }

    const cJSON* commands = cJSON_GetObjectItem(root, "commands");
    if (cJSON_IsObject(commands)) {
        const cJSON* command = nullptr;
        cJSON_ArrayForEach(command, commands)
        {
            if (profile_.commandCount >= kMaxAcCommands || !cJSON_IsArray(command)) {
                continue;
            }
            AcCommandDef& def = profile_.commands[profile_.commandCount];
            std::strncpy(def.name, command->string != nullptr ? command->string : "",
                         sizeof(def.name) - 1);

            const cJSON* opJson = nullptr;
            cJSON_ArrayForEach(opJson, command)
            {
                if (def.opCount >= kMaxAcCommandOps || !cJSON_IsObject(opJson)) {
                    continue;
                }
                AcCommandOp& op = def.ops[def.opCount];
                const cJSON* field = cJSON_GetObjectItem(opJson, "field");
                if (!cJSON_IsString(field) || !parseFieldIndex(field->valuestring, op.fieldIndex)) {
                    continue;
                }

                const cJSON* value = cJSON_GetObjectItem(opJson, "value");
                const cJSON* binaryOr = cJSON_GetObjectItem(opJson, "binary_or");
                if (cJSON_IsString(value)) {
                    op.kind = AcOpKind::SetValue;
                    const AcPlaceholder ph = placeholderFromToken(value->valuestring);
                    if (ph == AcPlaceholder::Literal) {
                        if (!parseHexByte(value->valuestring, op.literalValue)) {
                            continue;
                        }
                        op.placeholder = AcPlaceholder::Literal;
                    } else {
                        op.placeholder = ph;
                    }
                } else if (cJSON_IsArray(binaryOr)) {
                    op.kind = AcOpKind::BinaryOr;
                    op.literalMask = 0;
                    op.placeholder = AcPlaceholder::None;
                    const cJSON* token = nullptr;
                    cJSON_ArrayForEach(token, binaryOr)
                    {
                        if (!cJSON_IsString(token)) {
                            continue;
                        }
                        const AcPlaceholder ph = placeholderFromToken(token->valuestring);
                        if (ph == AcPlaceholder::Literal) {
                            uint8_t byte = 0;
                            if (parseHexByte(token->valuestring, byte)) {
                                op.literalMask = static_cast<uint8_t>(op.literalMask | byte);
                            }
                        } else {
                            op.placeholder = ph;
                        }
                    }
                } else {
                    continue;
                }
                def.opCount++;
            }

            if (def.name[0] != '\0' && def.opCount > 0) {
                profile_.commandCount++;
            }
        }
    }

    const cJSON* data = cJSON_GetObjectItem(root, "data");
    if (cJSON_IsArray(data)) {
        const cJSON* item = nullptr;
        cJSON_ArrayForEach(item, data)
        {
            if (profile_.dataRequestCount >= kMaxAcDataRequests || !cJSON_IsObject(item)) {
                continue;
            }
            AcDataRequest& req = profile_.dataRequests[profile_.dataRequestCount];
            const cJSON* name = cJSON_GetObjectItem(item, "name");
            const cJSON* bytes = cJSON_GetObjectItem(item, "bytes");
            if (!cJSON_IsString(name) || !cJSON_IsArray(bytes)) {
                continue;
            }
            std::strncpy(req.name, name->valuestring, sizeof(req.name) - 1);
            const cJSON* byteJson = nullptr;
            cJSON_ArrayForEach(byteJson, bytes)
            {
                if (req.length >= sizeof(req.bytes) || !cJSON_IsString(byteJson)) {
                    continue;
                }
                uint8_t value = 0;
                if (!parseHexByte(byteJson->valuestring, value)) {
                    continue;
                }
                req.bytes[req.length++] = value;
            }
            if (req.name[0] != '\0' && req.length > 0) {
                profile_.dataRequestCount++;
            }
        }
    }

    cJSON_Delete(root);
    if (profile_.commandCount == 0) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    std::memset(workingFrame_, 0, sizeof(workingFrame_));
    profile_.loaded = true;
    ESP_LOGI(kTag, "Loaded protocol commands=%u data=%u frame=%u",
             static_cast<unsigned>(profile_.commandCount),
             static_cast<unsigned>(profile_.dataRequestCount),
             static_cast<unsigned>(profile_.frameSize));
    return ESP_OK;
}

bool AcProtocolEngine::isLoaded() const
{
    return profile_.loaded;
}

const AcProtocolProfile& AcProtocolEngine::profile() const
{
    return profile_;
}

const AcCommandDef* AcProtocolEngine::findCommand(const char* name) const
{
    if (name == nullptr) {
        return nullptr;
    }
    for (size_t i = 0; i < profile_.commandCount; ++i) {
        if (std::strcmp(profile_.commands[i].name, name) == 0) {
            return &profile_.commands[i];
        }
    }
    return nullptr;
}

uint8_t AcProtocolEngine::encodePlaceholder(AcPlaceholder placeholder,
                                            const AcDesiredState& desired) const
{
    switch (placeholder) {
        case AcPlaceholder::Power:
            return desired.powerOn ? 0x01 : 0x00;
        case AcPlaceholder::Mode:
            switch (desired.mode) {
                case AcMode::Cool: return 0x01;
                case AcMode::Heat: return 0x02;
                case AcMode::Dry: return 0x03;
                case AcMode::Fan: return 0x04;
                case AcMode::Auto: return 0x05;
                case AcMode::Off:
                default: return 0x00;
            }
        case AcPlaceholder::Temp: {
            int temp = static_cast<int>(desired.targetTempC + 0.5f);
            if (temp < profile_.minTempC) {
                temp = profile_.minTempC;
            }
            if (temp > profile_.maxTempC) {
                temp = profile_.maxTempC;
            }
            return static_cast<uint8_t>(temp);
        }
        case AcPlaceholder::Fan:
            switch (desired.fan) {
                case AcFanSpeed::Low: return 0x01;
                case AcFanSpeed::Medium: return 0x02;
                case AcFanSpeed::High: return 0x03;
                case AcFanSpeed::Auto:
                default: return 0x00;
            }
        case AcPlaceholder::Literal:
            return 0;
        case AcPlaceholder::None:
        default:
            return 0;
    }
}

uint8_t AcProtocolEngine::placeholderMask(AcPlaceholder placeholder) const
{
    switch (placeholder) {
        case AcPlaceholder::Power: return 0x01;
        case AcPlaceholder::Mode: return 0x07;
        case AcPlaceholder::Fan: return 0x03;
        case AcPlaceholder::Temp: {
            // Every bit an encoded setpoint (clamped to maxTempC) can occupy.
            uint8_t mask = 0;
            while (mask < profile_.maxTempC && mask != 0xFF) {
                mask = static_cast<uint8_t>((mask << 1) | 1);
            }
            return mask;
        }
        default: return 0x00;
    }
}

esp_err_t AcProtocolEngine::applyCommand(const char* name, const AcDesiredState& desired)
{
    const AcCommandDef* command = findCommand(name);
    if (command == nullptr) {
        return ESP_ERR_NOT_FOUND;
    }

    for (size_t i = 0; i < command->opCount; ++i) {
        const AcCommandOp& op = command->ops[i];
        if (op.fieldIndex >= profile_.frameSize) {
            return ESP_ERR_INVALID_SIZE;
        }
        uint8_t value = op.literalValue;
        if (op.placeholder != AcPlaceholder::None && op.placeholder != AcPlaceholder::Literal) {
            value = encodePlaceholder(op.placeholder, desired);
        }
        if (op.kind == AcOpKind::BinaryOr) {
            // Clear the placeholder's own bit range first: a plain OR can set bits but never
            // clear them, so power-off or a mode change would be lost once a bit was set.
            const uint8_t domain = placeholderMask(op.placeholder);
            uint8_t& field = workingFrame_[op.fieldIndex];
            field = static_cast<uint8_t>((field & ~domain) | op.literalMask | value);
        } else {
            workingFrame_[op.fieldIndex] = value;
        }
    }
    return ESP_OK;
}

BusResult AcProtocolEngine::transmitFrame(BusHandle& bus, const uint8_t* data, size_t len)
{
    return exchange(bus, data, len, nullptr, 0);
}

BusResult AcProtocolEngine::exchange(BusHandle& bus, const uint8_t* tx, size_t txLen, uint8_t* rx,
                                     size_t rxLen)
{
    if (tx == nullptr || txLen == 0) {
        return BusResult::fail(BusStatus::InvalidArgument);
    }

    switch (bus.kind) {
        case BusKind::Spi: {
            if (!bus.spi) {
                return BusResult::fail(BusStatus::NotOpen);
            }
            if (rx != nullptr && rxLen >= txLen) {
                return bus.spi->transfer(tx, rx, txLen, pdMS_TO_TICKS(100));
            }
            return bus.spi->write(tx, txLen, pdMS_TO_TICKS(100));
        }
        case BusKind::Uart:
        case BusKind::Rs485: {
            if (!bus.stream) {
                return BusResult::fail(BusStatus::NotOpen);
            }
            BusResult written = bus.stream->write(tx, txLen);
            if (!written) {
                return written;
            }
            if (rx != nullptr && rxLen > 0) {
                return bus.stream->read(rx, rxLen, pdMS_TO_TICKS(100));
            }
            return written;
        }
        case BusKind::Can: {
            if (!bus.can) {
                return BusResult::fail(BusStatus::NotOpen);
            }
            CanFrame frame;
            frame.dlc = static_cast<uint8_t>(txLen > 8 ? 8 : txLen);
            std::memcpy(frame.data, tx, frame.dlc);
            BusResult sent = bus.can->send(frame, pdMS_TO_TICKS(100));
            if (!sent || rx == nullptr || rxLen == 0) {
                return sent;
            }
            CanFrame response;
            BusResult received = bus.can->receive(response, pdMS_TO_TICKS(100));
            if (!received) {
                return received;
            }
            const size_t copyLen = response.dlc < rxLen ? response.dlc : rxLen;
            std::memcpy(rx, response.data, copyLen);
            return BusResult::ok(copyLen);
        }
        default:
            return BusResult::fail(BusStatus::Unsupported);
    }
}

esp_err_t AcProtocolEngine::applyDesiredState(BusHandle& bus, const AcDesiredState& desired)
{
    if (!profile_.loaded) {
        return ESP_ERR_INVALID_STATE;
    }

    // Rebuild control fields from desired state using available commands.
    (void)applyCommand("set_power", desired);
    (void)applyCommand("set_mode", desired);
    (void)applyCommand("set_temperature", desired);
    (void)applyCommand("set_fan_speed", desired);

    const BusResult result = transmitFrame(bus, workingFrame_, profile_.frameSize);
    return result ? ESP_OK : ESP_FAIL;
}

esp_err_t AcProtocolEngine::poll(BusHandle& bus, AcReportedState& reported)
{
    reported = AcReportedState{};
    if (!profile_.loaded) {
        return ESP_ERR_INVALID_STATE;
    }

    for (size_t i = 0; i < profile_.dataRequestCount; ++i) {
        const AcDataRequest& req = profile_.dataRequests[i];
        uint8_t response[8] = {};
        const BusResult result =
            exchange(bus, req.bytes, req.length, response, sizeof(response));
        if (!result) {
            // Keep polling remaining registers; one timeout should not stop the loop.
            continue;
        }

        if (std::strcmp(req.name, "RETURN-AIR") == 0 && result.count > 0) {
            reported.returnAirC = static_cast<float>(response[0]);
            reported.valid = true;
        } else if (std::strcmp(req.name, "SET-TEMP") == 0 && result.count > 0) {
            reported.setTempC = static_cast<float>(response[0]);
            reported.valid = true;
        }
    }
    return ESP_OK;
}

}  // namespace opencomfort
