/*
OpenComfort Firmware
Licensed under GPLv3 for personal, educational, and non-commercial use.
Commercial use requires a commercial license.
*/

#include "src/Application/Application.hpp"

// Weak so the Unity test app (test_app/) can provide its own entry point.
extern "C" __attribute__((weak)) void app_main(void)
{
    opencomfort::Application::run();
}
