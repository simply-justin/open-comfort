set(OPENCOMFORT_OPTIONAL_REQUIRES)
if(CONFIG_OPENCOMFORT_ENABLE_MATTER)
    list(APPEND OPENCOMFORT_OPTIONAL_REQUIRES opencomfort_matter)
endif()

idf_component_register(
    SRCS
        main.cpp
        src/Application/Application.cpp
        src/Product/ProductDefinition.cpp
        src/Config/UserConfigStore.cpp
        src/Config/DeviceHostname.cpp
        src/Config/ConnectorSecretStore.cpp
        src/Config/FactoryReset.cpp
        src/Connectors/ConnectorManager.cpp
        src/Connectors/Web/RuntimeHttpConnector.cpp
        src/Connectors/Mqtt/HomeAssistantMqttConnector.cpp
        src/Connectors/Matter/MatterConnector.cpp
        src/Connectors/Matter/MatterPlatform.cpp
        src/Bus/BusConfig.cpp
        src/Bus/BusFactory.cpp
        src/Bus/UartTransport.cpp
        src/Bus/SpiTransport.cpp
        src/Bus/CanTransport.cpp
        src/Wifi/WiFiService.cpp
        src/Http/CaptivePortal.cpp
        src/Http/WebAssets.cpp
        src/Setup/AcSetupProvider.cpp
        src/Profiles/ProfileRepository.cpp
        src/Profiles/ProfileCrypto.cpp
        src/Profiles/HttpDownloader.cpp
        src/Runtime/RuntimeController.cpp
        src/Schedule/ScheduleService.cpp
        src/Logging/LogBuffer.cpp
        src/Update/UpdateService.cpp
        src/ThirdParty/qrcodegen/qrcodegen.c
        src/Modules/ModuleFactory.cpp
        src/Modules/Ac/MhiLink.cpp
        src/Modules/Ac/Ac.cpp
        src/Modules/Ac/AcProtocolEngine.cpp
    INCLUDE_DIRS
        "."
    EMBED_FILES
        www/index.html
        www/runtime.html
        www/css/app.css
        www/js/app.js
        www/js/runtime.js
        www/fonts/geist.woff2
    REQUIRES
        nvs_flash
        spiffs
        json
        esp_wifi
        esp_http_server
        esp_http_client
        esp_https_ota
        app_update
        mqtt
        espressif__mdns
        esp_netif
        esp_event
        driver
        vfs
        mbedtls
    PRIV_REQUIRES
        ${OPENCOMFORT_OPTIONAL_REQUIRES}
)

spiffs_create_partition_image(storage spiffs FLASH_IN_PROJECT)

# Task stacks are 4-8 KB; fail the build if a function would need more than 4 KB of stack.
target_compile_options(${COMPONENT_LIB} PRIVATE -Werror=stack-usage=4096)

# Select the MHI wiring and frame format together to match the controller.
set(OPENCOMFORT_MHI_BOARD "rev-a" CACHE STRING "MHI board: rev-a or xiao-s3-attic")
if(OPENCOMFORT_MHI_BOARD STREQUAL "xiao-s3-attic")
    set_property(SOURCE src/Product/ProductDefinition.cpp APPEND PROPERTY COMPILE_DEFINITIONS OC_MHI_XIAO_ATTIC)
elseif(NOT OPENCOMFORT_MHI_BOARD STREQUAL "rev-a")
    message(FATAL_ERROR "Unknown OPENCOMFORT_MHI_BOARD: ${OPENCOMFORT_MHI_BOARD}")
endif()
