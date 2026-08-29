#pragma once

#include <algorithm>
#include <array>

#include "../types.hpp"

namespace drom {

// A single reversible step edit (an undo/redo entry). Copying the full Step
// keeps the change exact (pitch, length, active/tie); old/new remember the
// before/after, plus each step's per-session "original note" so the +/- hint
// and the step-level Rest-undo stay consistent across undo/redo too. len_* is
// the pattern loop length so a whole-page duplicate can grow the loop and be
// reverted cleanly.
struct UndoEntry {
    uint8_t index {0};
    Step old_step {};
    Step new_step {};
    int16_t old_prev {-1};
    int16_t new_prev {-1};
    uint8_t len_before {0};
    uint8_t len_after {0};
};

// Depth of the per-editor undo/redo logs. ~21 bytes per entry -> ~1.3 KB for
// both stacks, well within RP2040 RAM.
constexpr std::size_t kUndoDepth = 32;
// Max number of separate undoable "batches" (multi-step ops write many entries
// that belong to ONE user action, e.g. paste/erase of a page).
constexpr std::size_t kUndoMarksMax = 24;

// Transient on-screen confirmation of the last editor hotkey.
enum EditorHint : uint8_t {
    kHintNone = 0,
    kHintCopy,
    kHintPaste,
    kHintUndo,
    kHintRedo,
    kHintDup,
};

// Logic around copy/paste + undo/redo:
// - Copy/Paste/Duplicate act on the whole visible PAGE (16 steps), not single
//   notes (per user design). The clipboard is a flat step buffer plus the
//   number of valid steps and the absolute index where step0 will land.
// - Undo/redo are command-based: every step edit pushes {index, old, new}.
//   Multi-step ops (paste, erase page) group their entries into one batch so a
//   single Undo reverts the whole action.
struct PtnEditorUI {
    uint8_t cur {0};       // absolute step index under the cursor
    uint8_t selected {0};  // step index that receives note-key input (0..63)
    bool has_selected {false}; // true when a step is selected for note editing
    uint8_t page {0};      // visible 16-step page (0 = steps 1..16)
    uint8_t field {0};     // focused value: 0=NOTE 1=LEN 2=ON 3=PLEN(pattern len)
    int16_t prev_note {-1}; // original note of the selected step (undo); -1 = none
    std::array<int16_t, kStepCountMax> prev_notes {}; // per-step original note

    // --- Range selection (universal mechanism, Phase 1) ---------------------
    // A selection is an inclusive step range [aLo..bHi] that stays visible
    // after leaving SELECT mode so any future operation (pitch, chop, ...) can
    // consume it. sel_mode is the dedicated SELECT sub-mode of the editor
    // (toggled by Shift+joy-click); here plain tilt grows/shrinks the movable
    // edge and Shift+tilt grows/shrinks the other one. Selection is a separate
    // concern from the single unified cursor/playhead marker (cur/selected),
    // so note editing, playing and selecting do not fight each other.
    bool sel_mode {false};    // SELECT sub-mode active
    bool sel_active {false};  // a range is currently marked
    uint8_t sel_a {0};        // first boundary (absolute step)
    uint8_t sel_b {0};        // second boundary (absolute step)

    // Clipboard (whole-page copy). clip_len is the number of valid steps.
    std::array<Step, kStepCountMax> clip {};
    uint8_t clip_len {0};
    uint8_t clip_anchor {0};  // absolute step where clip[0] resolved last paste

    // Undo / redo logs: flat entry buffers + batch boundaries (mark = entry
    // count at the START of the batch).
    std::array<UndoEntry, kUndoDepth> undo_buf {};
    uint8_t undo_size {0};
    std::array<uint8_t, kUndoMarksMax> undo_marks {};
    uint8_t undo_marks_count {0};
    uint8_t undo_len_before {0};  // loop length captured at the batch start
    std::array<UndoEntry, kUndoDepth> redo_buf {};
    uint8_t redo_size {0};
    std::array<uint8_t, kUndoMarksMax> redo_marks {};
    uint8_t redo_marks_count {0};

    // Transient on-screen confirmation of the last hotkey (0 = none), cleared
    // by the loop once hint_until_ms passes. Shown in the editor detail row.
    uint8_t hint {0};
    uint32_t hint_until_ms {0};
};

struct AppState {
    std::array<Pattern, kSlotCount> slots {};
    uint8_t current_slot {0};
    RuntimeState runtime {};
    PtnEditorUI editor {};

    Pattern& active_pattern() { return slots[current_slot]; }
    const Pattern& active_pattern() const { return slots[current_slot]; }
};

// Inclusive lower/upper bounds of the current selection (a and b are unordered
// boundaries). Returns false when there is no active range. Clamped to the
// given loop length so callers never touch steps beyond the pattern end.
inline bool selection_bounds(const PtnEditorUI& ed, int len, int& lo, int& hi) {
    if (!ed.sel_active) {
        return false;
    }
    int a = std::min<int>(static_cast<int>(ed.sel_a),
                          std::max<int>(0, len - 1));
    int b = std::min<int>(static_cast<int>(ed.sel_b),
                          std::max<int>(0, len - 1));
    lo = std::min(a, b);
    hi = std::max(a, b);
    return true;
}

void init_default_state(AppState& state);

}  // namespace drom