#pragma once

#include <string>
#include <vector>
#include <map>
#include <cstdint>

/**
 * @file ICommand.hpp
 * @brief Abstract interface for executable command objects.
 */

/**
 * @class ICommand
 * @brief Represents a named command that can be executed with parameters.
 *
 * Commands receive parameters as key/value pairs and return a raw byte
 * payload, typically intended for transmission over a bus.
 */
class ICommand {
public:
    virtual ~ICommand() = default;

    /**
     * @brief Get the unique command name.
     *
     * Used to identify the command and for command lookup/dispatch.
     *
     * @return The command name as a string.
     */
    virtual std::string name() const = 0;

    /**
     * @brief Execute the command with the given parameters.
     *
     * Each implementation is responsible for interpreting the parameters
     * and producing a byte sequence representing the command’s output.
     *
     * @param params Key/value parameter map (float values for flexibility).
     * @return Byte vector representing the command's output data.
     */
    virtual std::vector<uint8_t> execute(
        const std::map<std::string, float>& params
    ) = 0;
};
