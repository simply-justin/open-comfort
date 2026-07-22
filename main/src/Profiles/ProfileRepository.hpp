#pragma once

#include <string>

#include "esp_err.h"

#include "src/Product/ProductDefinition.hpp"
#include "src/Profiles/ProfileTypes.hpp"

namespace opencomfort {

class ProfileRepository {
public:
    static esp_err_t initSpiffs();
    static esp_err_t loadCatalog(ProfileCatalog& out);
    static esp_err_t refreshRemoteCatalog(const ProductDefinition& product);
    static esp_err_t loadProfile(const char* profileId, LoadedAcProfile& out);
    static esp_err_t loadProfileJson(const char* profileId, std::string& outJson);
    static esp_err_t ensureProfileAvailable(const ProductDefinition& product,
                                            const char* profileId,
                                            LoadedAcProfile& out);
};

}  // namespace opencomfort
