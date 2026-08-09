#pragma once

#include <cstdint>

namespace drom {

struct BoardPins {
    static constexpr uint8_t kShiftLatch = 2;
    static constexpr uint8_t kShiftClock = 3;
    static constexpr uint8_t kShiftData = 4;

    static constexpr uint8_t kJoyX = 26;
    static constexpr uint8_t kJoyY = 27;
    static constexpr uint8_t kJoyButton = 15;

    static constexpr uint8_t kI2cSda = 0;
    static constexpr uint8_t kI2cScl = 1;

    static constexpr uint8_t kLed = 25;  // onboard LED (PICO_DEFAULT_LED_PIN)
};

}  // namespace drom
