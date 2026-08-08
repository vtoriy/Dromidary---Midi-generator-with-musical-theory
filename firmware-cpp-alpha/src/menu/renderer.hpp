#pragma once

#include "../platform/display_sh1106.hpp"
#include "../state/state.hpp"
#include "animation.hpp"
#include "menu_engine.hpp"

namespace drom {

// Renders the current menu / status into the SH1106 framebuffer (no I2C push;
// the app loop calls display.flush() separately at a safe gap).
class MenuRenderer {
public:
    MenuRenderer(DisplaySh1106* display) : display_(display) {}

    void render(const AppState& state, const MenuEngine& engine);

private:
    DisplaySh1106* display_;
    AnimationRenderer animation_;
};

}  // namespace drom