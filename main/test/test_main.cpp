#include <cstdio>

#include "unity.h"
#include "unity_test_runner.h"

// Replaces the firmware's weak app_main in the test app.
extern "C" void app_main(void)
{
    UNITY_BEGIN();
    unity_run_all_tests();
    const int failures = UNITY_END();
    std::printf("OPENCOMFORT_TESTS_DONE failures=%d\n", failures);
}
