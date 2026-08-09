#include "state.hpp"

namespace drom {

namespace {

Step make_empty_step() {
    Step step {};
    step.active = false;
    step.tie = false;
    step.length_steps = 4;
    step.note_count = 0;
    return step;
}

}  // namespace

void init_default_state(AppState& state) {
    for (auto& slot : state.slots) {
        slot = {};
        slot.length = 16;
        slot.bpm = 120;
        for (auto& step : slot.steps) {
            step = make_empty_step();
        }
    }
    state.current_slot = 0;
    state.runtime = {};
}

}  // namespace drom