idf_component_register(
    SRCS
        main.cpp

        # Bus system
        src/Busses/BusFactory.cpp
        src/Busses/Uart.cpp
        src/Busses/Spi.cpp
        src/Busses/Can.cpp

        src/Core/WifiManager.cpp

        src/Configs/ConfigManager.cpp
        src/Configs/RemoteConfigProvider.cpp

        src/Http/CaptivePortal.cpp
        src/Http/WebPortal.cpp

        # Modules
        src/Modules/ModuleFactory.cpp
        src/Modules/Ac/Ac.cpp
        src/Modules/Ac/AcProfileLoader.cpp
        src/Modules/Ac/Commands/CommandResolver.cpp
        src/Modules/Ac/Commands/ProfileCommandEngine.cpp

        src/Protocols/Matter/MatterAdapter.cpp

    INCLUDE_DIRS
        "."
        "src/Busses"
        "src/Core"
        "src/Configs"
        "src/Http"
        "src/Modules"
    REQUIRES
        driver
        esp_event
        esp_http_client
        esp_http_server
        esp_netif
        esp-tls
        esp_wifi
        mbedtls
        json
        nvs_flash
        spiffs
)

spiffs_create_partition_image(storage spiffs FLASH_IN_PROJECT)
