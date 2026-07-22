#pragma once

#include "esp_err.h"

#include "src/Profiles/ProfileTypes.hpp"
#include "src/Product/ProductDefinition.hpp"

namespace opencomfort {

class ProfileRepository {
public:
    static esp_err_t initSpiffs();
    static esp_err_t loadCatalog(ProfileCatalog& out);
    static esp_err_t loadProfile(const char* profileId, LoadedAcProfile& out);
    static esp_err_t ensureProfileAvailable(const ProductDefinition& product,
                                            const char* profileId,
                                            LoadedAcProfile& out);
};

}  // namespace opencomfort
