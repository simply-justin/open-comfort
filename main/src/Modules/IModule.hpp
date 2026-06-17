#pragma once

/**
 * @file IModule.hpp
 * @brief Abstract interface for all functional device modules.
 *
 * Modules represent independent functional units such as AC controllers,
 * air purifiers, or other sub-systems. They are created by ModuleFactory
 * based on the persisted device configuration.
 */

/**
 * @class IModule
 * @brief Base interface for all device modules.
 *
 * Each module follows a simple lifecycle:
 *  1. bootstrap() — Perform initialization logic
 *  2. isBooted()  — Report whether boot was successful
 *  3. run()       — Main repeated execution loop
 */
class IModule {
public:
    virtual ~IModule() = default;

    /**
     * @brief Perform the module's startup sequence.
     *
     * Called exactly once after construction. Modules should allocate
     * resources, initialize communication channels, and prepare for
     * normal operation. On success, the module must internally mark
     * itself as "booted" so isBooted() returns true.
     */
    virtual void bootstrap() = 0;

    /**
     * @brief Returns whether the module successfully completed bootstrap().
     *
     * Used by the application to determine if the firmware can continue
     * into the main execution loop.
     *
     * @return true if the module is fully initialized and operational.
     */
    virtual bool isBooted() const = 0;

    /**
     * @brief Main execution callback.
     *
     * Called repeatedly by the application loop. Implementations may
     * block, yield, schedule asynchronous work, or process bus I/O.
     */
    virtual void run() = 0;
};
