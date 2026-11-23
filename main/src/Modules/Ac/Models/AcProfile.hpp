#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "src/Enums/BusType.hpp"

struct CommandStep {
    std::string field;
    std::string value;
    std::vector<std::string> binary_or;
};

struct DataQuery {
    std::string name;
    std::vector<uint8_t> bytes;
};

struct FrameConfig {
    bool enabled = false;
    std::vector<uint8_t> header;
    size_t payloadLength = 18;
    bool checksumSum8 = false;
};

struct AcProfile {
    std::string brand;
    BusType busType = BusType::SPI;
    int spiMode = 0;
    int minTemp = 16;
    int maxTemp = 30;
    FrameConfig frame;
    std::map<std::string, std::vector<CommandStep>> commands;
    std::vector<DataQuery> dataQueries;
};
