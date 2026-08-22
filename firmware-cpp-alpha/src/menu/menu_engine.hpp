#pragma once

#include "../state/state.hpp"
#include "../types.hpp"
#include "menu_items.hpp"

namespace drom {

constexpr int kMaxMenuFrames = 5;

struct MenuFrame {
    bool is_rows {false};
    const QuickRow* rows {nullptr};
    int row_count {0};
    const MenuItem* items {nullptr};
    int item_count {0};
    int cursor {0};
    int offset {0};
    int seg {0};
    // QUICK root: the header (mode switch KB/RND) is itself a selectable zone,
    // reached from row 0's left column (seg == -1) by tilting up. While it is
    // focused, a click toggles the play mode instead of editing a cell.
    bool header_focus {false};
};

// Navigation state machine for QUICK / DETAIL / MAIN / ANIM screens.
// Mirrors the Python MenuEngine (menu.py) semantics: click-edit cycle for
// QUICK cells, live left/right editing in DETAIL/MAIN, Rest+click reset,
// long-press pop / screen-mode switch.
class MenuEngine {
public:
    void init(AppState* state);
    void rebuild();

void tilt(Direction dir, bool shift);
    void radial_select(int zone);
    void press_short();
    void press_long();
    void reset_value();

    // --- renderer queries ---
    bool is_rows() const { return depth_ > 0 && stack_[depth_ - 1].is_rows; }
    int depth() const { return depth_; }
    bool edit_mode() const { return edit_mode_; }
    int32_t edit_snapshot() const { return edit_snapshot_; }
    int32_t item_snapshot() const { return item_snapshot_; }
    bool editing_radial() const;
    // Active 2D range edit for a NoteRange item in DETAIL/MAIN.
    bool editing_range() const { return range_edit_; }
    void snapshot_range();
    // True in DETAIL/MAIN when the focused item is an editable value
    // (i.e. double-click is meaningful as a reset).
    bool editing_value_item() const;
    // True in QUICK when the focused cell is being edited and its live value
    // differs from the last accepted value (renderer draws a marker).
    bool quick_edited() const;
    // True when the QUICK header zone (the mode switch KB/RND) has the focus.
    bool header_focus() const;

    const MenuFrame& current() const;
    const QuickRow* parent_row() const;
    const MenuItem* parent_item() const;
    const char* parent_label() const;

private:
    void push_rows(const QuickRow* rows, int count);
    void push_items(const MenuItem* items, int count);
    void pop();
    void enter_edit(const Segment& seg);
    void move_row(int delta);
    void move_seg(int delta);
    void move_cursor(int delta);
    void snapshot_focused_item();
    void snapshot_cell();
    const Segment* current_segment() const;
    const MenuItem* current_item() const;
    void item_adjust(int delta);
    void item_jump(bool to_min);

    AppState* st_ {nullptr};
    MenuContent content_ {};
    std::array<MenuFrame, kMaxMenuFrames> stack_ {};
    int depth_ {0};
    bool edit_mode_ {false};
    int32_t edit_snapshot_ {0};
    int32_t item_snapshot_ {0};
    int32_t cell_accepted_ {0};  // last accepted value of the focused QUICK cell
    bool range_edit_ {false};    // 2D range editing active on a NoteRange item
    int32_t range_snap_min_ {12};
    int32_t range_snap_max_ {119};
    int32_t cell_range_min_ {12};  // snapped min of the focused Range cell
    int32_t cell_range_max_ {119}; // snapped max of the focused Range cell
};

}  // namespace drom