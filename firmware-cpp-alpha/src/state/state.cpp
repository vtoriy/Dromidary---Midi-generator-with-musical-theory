#include "state.hpp"

namespace drom {

namespace {

Step make_empty_step() {
    Step step {};
    step.active = false;
    step.tie = false;
    step.len_div = 8;  // "8" = 1/8 note
    step.note_count = 0;
    return step;
}

}  // namespace

void init_default_state(AppState& state) {
    for (auto& slot : state.slots) {
        slot = {};
        slot.length = 16;
        slot.timing.bpm = 120;
        for (auto& step : slot.steps) {
            step = make_empty_step();
        }
    }
    state.current_slot = 0;
    state.runtime = {};
    for (auto& pn : state.editor.prev_notes) {
        pn = -1;  // no per-step "original note" recorded yet
    }
    state.editor.prev_note = -1;
}

}  // namespace drom