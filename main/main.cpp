/*
OpenComfort Firmware
Licensed under GPLv3 for personal, educational, and non-commercial use.
Commercial use requires a commercial license.
*/

#include "src/Application/Application.hpp"

extern "C" void app_main(void)
{
    opencomfort::Application::run();
}
