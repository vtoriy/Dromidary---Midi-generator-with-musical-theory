#pragma once

#include <array>

#include "../types.hpp"

namespace drom {

struct AppState {
    std::array<Pattern, kSlotCount> slots {};
    uint8_t current_slot {0};
    RuntimeState runtime {};

    Pattern& active_pattern() { return slots[current_slot]; }
    const Pattern& active_pattern() const { return slots[current_slot]; }
};

void init_default_state(AppState& state);

}  // namespace drom