idf_component_register(
    SRCS
        main.cpp

        # Bus system
        src/Busses/BusFactory.cpp
        src/Busses/Uart.cpp
        src/Busses/Spi.cpp
        src/Busses/Can.cpp

        src/Http/CaptivePortal.cpp

        # Modules
        src/Modules/ModuleFactory.cpp
        src/Modules/Ac/Ac.cpp

    INCLUDE_DIRS
        "."
)
