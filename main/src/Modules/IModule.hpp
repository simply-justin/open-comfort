#pragma once

/**
 * @brief Base interface for all device modules (AC, Fan, Purifier, etc.)
 * A module is created by ModuleFactory based on the stored configuration.
 */
class IModule {
public:
    virtual ~IModule() = default;

    /**
     * @brief Called once to start the module.
     *        Should set internal boot flag to true if successful.
     */
    virtual void bootstrap() = 0;

    /**
     * @brief Returns true if module booted successfully.
     */
    virtual bool isBooted() const = 0;

    /**
     * @brief Main execution loop, called repeatedly.
     */
    virtual void run() = 0;
};
