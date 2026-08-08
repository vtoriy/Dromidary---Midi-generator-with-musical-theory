#include "joystick.hpp"

#include "hardware/adc.h"
#include "hardware/gpio.h"

#include "board_pins.hpp"

namespace drom {

namespace {
constexpr uint16_t kCenter = 2048;   // 12-bit ADC midpoint
constexpr int kDeadzone = 900;       // analog threshold
}  // namespace

void Joystick::init() {
    adc_init();
    adc_gpio_init(BoardPins::kJoyX);
    adc_gpio_init(BoardPins::kJoyY);

    gpio_init(BoardPins::kJoyButton);
    gpio_set_dir(BoardPins::kJoyButton, GPIO_IN);
    gpio_pull_up(BoardPins::kJoyButton);
}

void Joystick::poll() {
    adc_select_input(0);
    const int x = adc_read();
    adc_select_input(1);
    const int y = adc_read();
    btn_ = (gpio_get(BoardPins::kJoyButton) == 0);

    int dx = 0;
    int dy = 0;
    if (x < kCenter - kDeadzone) { dx = -1; }
    else if (x > kCenter + kDeadzone) { dx = 1; }
    if (y < kCenter - kDeadzone) { dy = -1; }   // pushed up -> y low
    else if (y > kCenter + kDeadzone) { dy = 1; }

    if (dx == 0 && dy == 0) {
        dir_ = Direction::Center;
    } else if (dy == -1 && dx == 0) {
        dir_ = Direction::Up;
    } else if (dy == 1 && dx == 0) {
        dir_ = Direction::Down;
    } else if (dx == 1 && dy == 0) {
        dir_ = Direction::Right;
    } else if (dx == -1 && dy == 0) {
        dir_ = Direction::Left;
    } else if (dy == -1 && dx == 1) {
        dir_ = Direction::UpRight;
    } else if (dy == -1 && dx == -1) {
        dir_ = Direction::UpLeft;
    } else if (dy == 1 && dx == 1) {
        dir_ = Direction::DownRight;
    } else {
        dir_ = Direction::DownLeft;
    }
}

}  // namespace drom