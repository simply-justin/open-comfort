#pragma once

#include <string>
#include <vector>
#include <optional>

#include "src/Modules/Ac/Models/Profile.hpp"

/**
 * @file CommandResolver.hpp
 * @brief Resolves command names to protocol command sequences.
 */

/**
 * @class CommandResolver
 * @brief Utility for resolving AC protocol commands based on profile data.
 *
 * Looks up command definitions inside an ACProfile and selects the
 * appropriate command sequence for the given model, family, and command name.
 */
class CommandResolver {
public:
    /**
     * @brief Resolve a command into its concrete sequence.
     *
     * Returns the list of command tokens or frames as strings, depending
     * on how the ACProfile encodes them. If the lookup fails, std::nullopt
     * is returned.
     *
     * @param profile Parsed AC profile containing model/family definitions.
     * @param model   Target AC model identifier.
     * @param family  Target AC family identifier.
     * @param commandName Logical command name (e.g., "PowerOn", "TempUp").
     *
     * @return Optional list of resolved command strings.
     */
    static std::optional<std::vector<std::string>> resolve(
        const Profile &profile,
        const std::string &model,
        const std::string &family,
        const std::string &commandName
    );
};
