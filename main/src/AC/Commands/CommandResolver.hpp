#pragma once
#include <string>
#include <vector>
#include <optional>
#include "Ac/Models/ACProfile.hpp"

class CommandResolver {
public:
    static std::optional<std::vector<std::string>> resolve(
        const ACProfile &profile,
        const std::string &model,
        const std::string &family,
        const std::string &commandName
    );
};
