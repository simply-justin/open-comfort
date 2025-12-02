#pragma once

#include <string>
#include <vector>
#include <map>
#include <cstdint>

class ICommand {
public:
    virtual ~ICommand() = default;
    virtual std::string name() const = 0;
    virtual std::vector<uint8_t> execute(
        const std::map<std::string, float>& params
    ) = 0;
};
