#pragma once

#include "../menu/menu_items.hpp"
#include <cstdint>

namespace drom {

// KY-023 analog joystick (X, Y ADC + button). Poll() reads the current
// direction (8-way + center) and button state.
class Joystick {
public:
    void init();
    void poll();

    Direction direction() const { return dir_; }
    bool button() const { return btn_; }  // true = pressed

private:
    Direction dir_ {Direction::Center};
    bool btn_ {false};
};

}  // namespace drom