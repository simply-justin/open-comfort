#include "src/Modules/Ac/Commands/CommandResolver.hpp"

std::optional<std::vector<std::string>> CommandResolver::resolve(
    const Profile &profile,
    const std::string &model,
    const std::string &family,
    const std::string &commandName
) {
    /*
     * Resolution order:
     * 1. Model-level override
     * 2. Family-level override
     * 3. Root-level command template
     *
     * Each level optionally overrides the previous one.
     */

    // 1. Model-level override
    if (profile.modelCommands.count(model)) {
        auto &mc = profile.modelCommands.at(model);
        if (mc.count(commandName))
            return mc.at(commandName);
    }

    // 2. Family-level override
    if (profile.familyCommands.count(family)) {
        auto &fc = profile.familyCommands.at(family);
        if (fc.count(commandName))
            return fc.at(commandName);
    }

    // 3. Brand-level (root) default commands
    if (profile.commandTemplates.count(commandName))
        return profile.commandTemplates.at(commandName);

    // No match in any layer
    return std::nullopt;
}
