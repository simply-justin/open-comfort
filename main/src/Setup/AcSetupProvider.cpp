#include "src/Setup/AcSetupProvider.hpp"

#include <cstdio>
#include <cstring>

#include "sdkconfig.h"

#include "src/Product/ProductDefinition.hpp"
#include "src/Profiles/ProfileRepository.hpp"

namespace opencomfort {
namespace {

void setError(char* error, size_t errorLen, const char* message)
{
    if (error == nullptr || errorLen == 0) {
        return;
    }
    std::strncpy(error, message, errorLen - 1);
    error[errorLen - 1] = '\0';
}

}  // namespace

const char* AcSetupProvider::moduleId() const
{
    return "ac";
}

bool AcSetupProvider::buildSchema(SetupSchema& out)
{
    out = SetupSchema{};
    std::strncpy(out.moduleId, "ac", sizeof(out.moduleId) - 1);

    ProfileCatalog catalog;
    if (ProfileRepository::loadCatalog(catalog) != ESP_OK || catalog.count == 0) {
        return false;
    }

    SetupStep& step = out.steps[0];
    std::strncpy(step.id, "ac_profile", sizeof(step.id) - 1);
    std::strncpy(step.title, "Air conditioner", sizeof(step.title) - 1);
    std::strncpy(step.description, "Select your AC make and model.", sizeof(step.description) - 1);

    SetupField& makeField = step.fields[0];
    std::strncpy(makeField.id, "make", sizeof(makeField.id) - 1);
    std::strncpy(makeField.label, "Make", sizeof(makeField.label) - 1);
    makeField.type = SetupFieldType::Select;
    makeField.required = true;

    SetupField& modelField = step.fields[1];
    std::strncpy(modelField.id, "model", sizeof(modelField.id) - 1);
    std::strncpy(modelField.label, "Model", sizeof(modelField.label) - 1);
    modelField.type = SetupFieldType::Select;
    modelField.required = true;

    SetupField& nameField = step.fields[2];
    std::strncpy(nameField.id, "friendlyName", sizeof(nameField.id) - 1);
    std::strncpy(nameField.label, "Device name", sizeof(nameField.label) - 1);
    nameField.type = SetupFieldType::Text;
    nameField.required = true;

    for (size_t i = 0; i < catalog.count; ++i) {
        const ProfileCatalogEntry& entry = catalog.entries[i];
        bool makeExists = false;
        for (size_t j = 0; j < makeField.optionCount; ++j) {
            if (std::strcmp(makeField.options[j].value, entry.makeId) == 0) {
                makeExists = true;
                break;
            }
        }
        if (!makeExists && makeField.optionCount < kMaxFieldOptions) {
            std::strncpy(makeField.options[makeField.optionCount].value, entry.makeId,
                         sizeof(makeField.options[0].value) - 1);
            std::strncpy(makeField.options[makeField.optionCount].label, entry.makeName,
                         sizeof(makeField.options[0].label) - 1);
            makeField.optionCount++;
        }

        for (size_t m = 0; m < entry.modelCount && modelField.optionCount < kMaxFieldOptions; ++m) {
            char value[49] = {};
            std::snprintf(value, sizeof(value), "%s|%s", entry.id, entry.models[m]);
            std::strncpy(modelField.options[modelField.optionCount].value, value,
                         sizeof(modelField.options[0].value) - 1);
            std::snprintf(modelField.options[modelField.optionCount].label,
                          sizeof(modelField.options[0].label), "%s (%s)", entry.models[m],
                          entry.makeName);
            modelField.optionCount++;
        }
    }

    step.fieldCount = 3;
    out.stepCount = 1;
    return makeField.optionCount > 0 && modelField.optionCount > 0;
}

bool AcSetupProvider::validateDraft(const SetupDraft& draft, char* error, size_t errorLen) const
{
    if (draft.profileId[0] == '\0' || draft.modelId[0] == '\0') {
        setError(error, errorLen, "Select an AC make and model.");
        return false;
    }

    LoadedAcProfile profile;
    if (ProfileRepository::ensureProfileAvailable(currentProduct(), draft.profileId, profile) !=
        ESP_OK) {
        setError(error, errorLen, "Selected AC profile is unavailable.");
        return false;
    }
    return true;
}

bool AcSetupProvider::finalize(const SetupDraft& draft, UserConfig& out, char* error,
                               size_t errorLen) const
{
    if (!validateDraft(draft, error, errorLen)) {
        return false;
    }

    LoadedAcProfile profile;
    if (ProfileRepository::ensureProfileAvailable(currentProduct(), draft.profileId, profile) !=
        ESP_OK) {
        setError(error, errorLen, "Failed to load AC profile.");
        return false;
    }

    out = UserConfig{};
    out.schemaVersion = kUserConfigSchemaVersion;
    out.provisioned = true;
    out.module = ModuleKind::Ac;
    copyBounded(out.friendlyName, sizeof(out.friendlyName), draft.friendlyName);
    copyBounded(out.ac.profileId, sizeof(out.ac.profileId), profile.profileId);
    copyBounded(out.ac.profileVersion, sizeof(out.ac.profileVersion), profile.profileVersion);
    copyBounded(out.ac.modelId, sizeof(out.ac.modelId), draft.modelId);
    out.ac.bus = profile.bus;
    out.connectors = draft.connectors;
    return true;
}

ISetupProvider& currentSetupProvider()
{
#if CONFIG_OPENCOMFORT_MODULE_AC
    static AcSetupProvider provider;
    return provider;
#else
#error "No setup provider for selected module"
#endif
}

}  // namespace opencomfort
