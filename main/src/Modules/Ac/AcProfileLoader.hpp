#pragma once

#include <string>

#include "src/Modules/Ac/Models/AcProfile.hpp"

class AcProfileLoader {
public:
    static bool load(const std::string& path, AcProfile& profile, std::string* error = nullptr);
};
