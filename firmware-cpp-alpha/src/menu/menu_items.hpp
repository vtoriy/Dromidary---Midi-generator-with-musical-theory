#pragma once

#include "../state/state.hpp"
#include "../types.hpp"

namespace drom {

constexpr std::size_t kMaxMenuItems = 96;
constexpr int kMaxQuickRows = 6;

// Storage pool: the menubuilders emit items/rows into these fixed arrays during
// each rebuild. Pointers into `items` stay stable because it is a std::array.
struct MenuContent {
    std::array<MenuItem, kMaxMenuItems> items {};
    std::size_t item_count {0};
    std::array<QuickRow, kMaxQuickRows> rows {};
    int row_count {0};
    const MenuItem* full_root {nullptr};
    int full_root_count {0};
};

// Direction of one joystick tilt / navigation step.
enum class Direction : uint8_t {
    Center = 0, Left, Right, Up, Down, UpRight, UpLeft, DownRight, DownLeft,
};

// Builds the QUICK (Level 1) panel rows for the given mode.
void build_quick_rows(AppState*, MenuContent&);

// Builds the MAIN (full menu) item tree.
void build_full_menu(AppState*, MenuContent&);

// Direction -> radial zone (0..7), or -1 when centered.
int direction_to_zone(Direction dir);

}  // namespace drom