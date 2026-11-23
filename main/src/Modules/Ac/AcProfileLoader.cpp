#include "src/Modules/Ac/AcProfileLoader.hpp"

#include <cstdlib>

extern "C" {
    #include "cJSON.h"
    #include "esp_log.h"
}

#include "src/Configs/ConfigManager.hpp"

static const char* TAG = "AcProfileLoader";

static BusType parseBusType(const std::string& value) {
    if (value == "spi") return BusType::SPI;
    if (value == "uart") return BusType::UART;
    if (value == "rs485") return BusType::RS485;
    if (value == "can") return BusType::CAN;
    return BusType::UNKNOWN;
}

static int parseSpiMode(const std::string& value) {
    if (value == "mode0") return 0;
    if (value == "mode1") return 1;
    if (value == "mode2") return 2;
    if (value == "mode3") return 3;
    return 0;
}

static bool parseHexByte(const std::string& token, uint8_t& out) {
    if (token.size() != 2) return false;
    char* end = nullptr;
    unsigned long value = strtoul(token.c_str(), &end, 16);
    if (end == token.c_str() || *end != '\0' || value > 0xFF) return false;
    out = static_cast<uint8_t>(value);
    return true;
}

static bool parseDataBytes(cJSON* array, std::vector<uint8_t>& out) {
    if (!cJSON_IsArray(array)) return false;
    cJSON* item = nullptr;
    cJSON_ArrayForEach(item, array) {
        if (!cJSON_IsString(item)) return false;
        uint8_t byte = 0;
        if (!parseHexByte(item->valuestring, byte)) return false;
        out.push_back(byte);
    }
    return !out.empty();
}

static bool parseCommandSteps(cJSON* array, std::vector<CommandStep>& steps) {
    if (!cJSON_IsArray(array)) return false;

    cJSON* stepJson = nullptr;
    cJSON_ArrayForEach(stepJson, array) {
        if (!cJSON_IsObject(stepJson)) return false;

        CommandStep step;
        cJSON* field = cJSON_GetObjectItem(stepJson, "field");
        if (!cJSON_IsString(field)) return false;
        step.field = field->valuestring;

        cJSON* value = cJSON_GetObjectItem(stepJson, "value");
        if (cJSON_IsString(value)) step.value = value->valuestring;

        cJSON* binaryOr = cJSON_GetObjectItem(stepJson, "binary_or");
        if (cJSON_IsArray(binaryOr)) {
            cJSON* token = nullptr;
            cJSON_ArrayForEach(token, binaryOr) {
                if (!cJSON_IsString(token)) return false;
                step.binary_or.push_back(token->valuestring);
            }
        }

        if (step.value.empty() && step.binary_or.empty()) return false;
        steps.push_back(std::move(step));
    }

    return !steps.empty();
}

bool AcProfileLoader::load(const std::string& path, AcProfile& profile, std::string* error) {
    profile = AcProfile{};

    std::string json = ConfigManager::readTextFile(path.c_str());
    if (json.empty()) {
        if (error) *error = "profile file is missing or empty";
        return false;
    }

    cJSON* root = cJSON_Parse(json.c_str());
    if (!root) {
        if (error) *error = "profile JSON parse failed";
        return false;
    }

    cJSON* brand = cJSON_GetObjectItem(root, "brand");
    if (!cJSON_IsString(brand)) {
        if (error) *error = "profile.brand is required";
        cJSON_Delete(root);
        return false;
    }
    profile.brand = brand->valuestring;

    cJSON* protocol = cJSON_GetObjectItem(root, "protocol");
    if (!cJSON_IsObject(protocol)) {
        if (error) *error = "profile.protocol is required";
        cJSON_Delete(root);
        return false;
    }

    cJSON* bus = cJSON_GetObjectItem(protocol, "bus");
    if (!cJSON_IsString(bus)) {
        if (error) *error = "profile.protocol.bus is required";
        cJSON_Delete(root);
        return false;
    }

    profile.busType = parseBusType(bus->valuestring);
    if (profile.busType == BusType::UNKNOWN) {
        if (error) *error = "unsupported profile bus type";
        cJSON_Delete(root);
        return false;
    }

    cJSON* mode = cJSON_GetObjectItem(protocol, "mode");
    if (cJSON_IsString(mode)) profile.spiMode = parseSpiMode(mode->valuestring);

    cJSON* temperature = cJSON_GetObjectItem(root, "temperature");
    if (cJSON_IsObject(temperature)) {
        cJSON* minTemp = cJSON_GetObjectItem(temperature, "min");
        cJSON* maxTemp = cJSON_GetObjectItem(temperature, "max");
        if (cJSON_IsNumber(minTemp)) profile.minTemp = minTemp->valueint;
        if (cJSON_IsNumber(maxTemp)) profile.maxTemp = maxTemp->valueint;
    }

    cJSON* frame = cJSON_GetObjectItem(root, "frame");
    if (cJSON_IsObject(frame)) {
        cJSON* enabled = cJSON_GetObjectItem(frame, "enabled");
        profile.frame.enabled = cJSON_IsTrue(enabled);

        cJSON* payloadLength = cJSON_GetObjectItem(frame, "payloadLength");
        if (cJSON_IsNumber(payloadLength) && payloadLength->valueint > 0) {
            profile.frame.payloadLength = static_cast<size_t>(payloadLength->valueint);
        }

        cJSON* checksum = cJSON_GetObjectItem(frame, "checksum");
        if (cJSON_IsString(checksum) && std::string(checksum->valuestring) == "sum8") {
            profile.frame.checksumSum8 = true;
        }

        cJSON* header = cJSON_GetObjectItem(frame, "header");
        if (cJSON_IsArray(header)) {
            cJSON* byte = nullptr;
            cJSON_ArrayForEach(byte, header) {
                if (!cJSON_IsString(byte)) continue;
                uint8_t value = 0;
                if (parseHexByte(byte->valuestring, value)) {
                    profile.frame.header.push_back(value);
                }
            }
        }
    }

    cJSON* commands = cJSON_GetObjectItem(root, "commands");
    if (!cJSON_IsObject(commands)) {
        if (error) *error = "profile.commands is required";
        cJSON_Delete(root);
        return false;
    }

    cJSON* commandItem = commands->child;
    while (commandItem) {
        if (!cJSON_IsArray(commandItem)) {
            if (error) *error = "profile command must be an array";
            cJSON_Delete(root);
            return false;
        }

        std::vector<CommandStep> steps;
        if (!parseCommandSteps(commandItem, steps)) {
            if (error) *error = std::string("invalid command steps for ") + commandItem->string;
            cJSON_Delete(root);
            return false;
        }

        profile.commands[commandItem->string] = std::move(steps);
        commandItem = commandItem->next;
    }

    cJSON* data = cJSON_GetObjectItem(root, "data");
    if (cJSON_IsArray(data)) {
        cJSON* entry = nullptr;
        cJSON_ArrayForEach(entry, data) {
            if (!cJSON_IsObject(entry)) continue;

            cJSON* name = cJSON_GetObjectItem(entry, "name");
            cJSON* bytes = cJSON_GetObjectItem(entry, "bytes");
            if (!cJSON_IsString(name) || !cJSON_IsArray(bytes)) continue;

            DataQuery query;
            query.name = name->valuestring;
            if (parseDataBytes(bytes, query.bytes)) {
                profile.dataQueries.push_back(std::move(query));
            }
        }
    }

    cJSON_Delete(root);
    ESP_LOGI(TAG, "Loaded profile %s (%s) with %u commands",
             path.c_str(), profile.brand.c_str(), (unsigned)profile.commands.size());
    return true;
}
