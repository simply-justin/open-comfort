#pragma once

#include "AC/Enums/Mode.hpp"
#include "AC/Enums/FanSpeed.hpp"
#include "AC/Enums/FanControl.hpp"

struct State {
    bool powerOn = false;
    bool defrost = false;
    bool ThreeDAuto = false;

    float currentTemp = 22.0f;
    float targetTemp = 22.0f;

    Mode mode = Mode::Off;
    FanSpeed fanSpeed = FanSpeed::Auto;
    FanControlHorizontal fanHorizontal = FanControlHorizontal::Center;
    FanControlVertical fanVertical = FanControlVertical::Down_Center;
};
