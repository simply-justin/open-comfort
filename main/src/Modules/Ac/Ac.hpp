#pragma once

#include <string>
#include "src/Modules/IModule.hpp"

/**
 * @file Ac.hpp
 * @brief Air-conditioner control module implementation.
 *
 * This module encapsulates the logic required to control an AC unit.
 * It is instantiated by ModuleFactory based on the device configuration.
 */

/**
 * @class Ac
 * @brief AC device module implementing the IModule lifecycle.
 *
 * The AC module manages initialization, state transitions,
 * communication with the underlying bus, and continuous runtime logic.
 */
class Ac : public IModule {
public:
    Ac() = default;
    ~Ac() override = default;

    /**
     * @brief Perform AC module initialization.
     *
     * Responsible for setting up communication channels, validating
     * configuration, and preparing the AC logic for operation.
     *
     * Sets booted_ to true on success.
     */
    void bootstrap() override;

    /**
     * @brief Returns true if the module has successfully booted.
     */
    bool isBooted() const override { return booted_; }

    /**
     * @brief Main AC control loop.
     *
     * Called repeatedly by the application. Responsible for
     * executing temperature logic, reading/writing to the bus,
     * and reacting to system events.
     */
    void run() override;

private:
    bool booted_ = false; ///< Indicates successful module initialization.
};
