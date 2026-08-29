#include "menu_engine.hpp"

#include <algorithm>
#include <cstring>

#include "../engine/midi_chain.hpp"

namespace drom {

void MenuEngine::init(AppState* state) {
    st_ = state;
    rebuild();
}

void MenuEngine::rebuild() {
    depth_ = 0;
    edit_mode_ = false;
    edit_snapshot_ = 0;
    item_snapshot_ = 0;
    range_edit_ = false;

    switch (st_->runtime.screen_mode) {
        case ScreenMode::Quick:
            build_quick_rows(st_, content_);
            if (content_.row_count > 0) {
                push_rows(content_.rows.data(), content_.row_count);
            }
            break;
        case ScreenMode::Full:
            build_full_menu(st_, content_);
            if (content_.full_root_count > 0) {
                push_items(content_.full_root, content_.full_root_count);
            }
            break;
        case ScreenMode::Edit:
        case ScreenMode::Animation:
        default:
            break;  // no editable menu content (editor draws itself)
    }
}

void MenuEngine::push_rows(const QuickRow* rows, int count) {
    MenuFrame f;
    f.is_rows = true;
    f.rows = rows;
    f.row_count = count;
    f.cursor = 0;
    f.offset = 0;
    f.seg = 0;
    f.header_focus = false;
    stack_[depth_++] = f;
    snapshot_cell();
}

void MenuEngine::push_items(const MenuItem* items, int count) {
    MenuFrame f;
    f.is_rows = false;
    f.items = items;
    f.item_count = count;
    f.cursor = 0;
    f.offset = 0;
    stack_[depth_++] = f;
    edit_mode_ = false;
    snapshot_focused_item();
}

void MenuEngine::pop() {
    if (depth_ > 0) {
        --depth_;
    }
    edit_mode_ = false;
    range_edit_ = false;
    if (depth_ > 0 && !stack_[depth_ - 1].is_rows) {
        snapshot_focused_item();
    }
}

const MenuFrame& MenuEngine::current() const {
    return stack_[depth_ - 1];
}

bool MenuEngine::header_focus() const {
    return depth_ > 0 && stack_[depth_ - 1].is_rows &&
           stack_[depth_ - 1].header_focus;
}

const QuickRow* MenuEngine::parent_row() const {
    if (depth_ > 1 && stack_[depth_ - 2].is_rows) {
        const MenuFrame& f = stack_[depth_ - 2];
        if (f.cursor >= 0 && f.cursor < f.row_count) {
            return &f.rows[f.cursor];
        }
    }
    return nullptr;
}

const MenuItem* MenuEngine::parent_item() const {
    if (depth_ > 1 && !stack_[depth_ - 2].is_rows) {
        const MenuFrame& f = stack_[depth_ - 2];
        if (f.cursor >= 0 && f.cursor < f.item_count) {
            return &f.items[f.cursor];
        }
    }
    return nullptr;
}

const char* MenuEngine::parent_label() const {
    const QuickRow* row = parent_row();
    if (row != nullptr) {
        return row->label ? row->label : "<PARENT";
    }
    const MenuItem* item = parent_item();
    if (item != nullptr) {
        return item->label ? item->label : "<PARENT";
    }
    return "<PARENT";
}

const Segment* MenuEngine::current_segment() const {
    if (depth_ == 0 || !is_rows()) {
        return nullptr;
    }
    const MenuFrame& f = current();
    if (f.header_focus) {
        return nullptr;
    }
    if (f.cursor < 0 || f.cursor >= f.row_count) {
        return nullptr;
    }
    const QuickRow& row = f.rows[f.cursor];
    if (f.seg < 0 || f.seg >= row.seg_count) {
        return nullptr;
    }
    return &row.segments[f.seg];
}

const MenuItem* MenuEngine::current_item() const {
    if (depth_ == 0 || is_rows()) {
        return nullptr;
    }
    const MenuFrame& f = current();
    if (f.cursor < 0 || f.cursor >= f.item_count) {
        return nullptr;
    }
    return &f.items[f.cursor];
}

bool MenuEngine::editing_radial() const {
    if (!edit_mode_ || !is_rows()) {
        return false;
    }
    const Segment* seg = current_segment();
    return seg != nullptr && seg->type == SegmentType::Radial;
}

bool MenuEngine::editing_value_item() const {
    if (depth_ == 0 || is_rows()) {
        return false;
    }
    const MenuItem* item = current_item();
    if (item == nullptr || !item->set_i) {
        return false;
    }
    return item->type == MenuItemType::Toggle ||
           item->type == MenuItemType::Option ||
           item->type == MenuItemType::IntSlider ||
           item->type == MenuItemType::Rate;
}

void MenuEngine::move_row(int delta) {
    if (!is_rows()) {
        return;
    }
    MenuFrame& f = stack_[depth_ - 1];
    if (f.row_count == 0) {
        return;
    }
    // Header zone (mode switch): Up is a no-op (already at the top), Down
    // returns to row 0 keeping the column the user came from.
    if (f.header_focus) {
        if (delta > 0) {
            f.header_focus = false;
            f.cursor = 0;
            snapshot_cell();
        }
        return;
    }
    // Keep the same column when moving vertically: moving down from the 2nd
    // segment of a row lands on the 2nd segment of the row below (clamped to
    // the target row's segment count). Tilting up from the TOP ROW — from any
    // column, not just the row-name gutter — moves the focus onto the header
    // zone (the KB/RND switch).
    const int old_seg = f.seg;
    const int max_i = f.row_count - 1;
    const int new_cur = std::clamp(f.cursor + delta, 0, max_i);
    if (delta < 0 && f.cursor == 0) {
        f.header_focus = true;
        f.cursor = 0;
        return;
    }
    if (new_cur < f.offset) {
        f.offset = new_cur;
    } else if (new_cur >= f.offset + 5) {
        f.offset = new_cur - 4;
    }
    f.cursor = new_cur;
    const int target_segs = f.rows[new_cur].seg_count;
    // -1 = the row label (leftmost column), the only place where click opens
    // the row's DETAIL submenu.
    f.seg = (target_segs > 0) ? std::clamp(old_seg, -1, target_segs - 1) : -1;
    snapshot_cell();
}

void MenuEngine::move_seg(int delta) {
    if (!is_rows()) {
        return;
    }
    MenuFrame& f = stack_[depth_ - 1];
    // Tilting sideways while the header (mode switch) is focused drops back to
    // row 0 and continues from the column the user came from.
    if (f.header_focus) {
        f.header_focus = false;
        f.cursor = 0;
    }
    const QuickRow& row = f.rows[f.cursor];
    // -1 is the row caption column; moving left from cell 0 lands on it when
    // the row carries a DETAIL submenu (seg = nullptr triggers the push).
    f.seg = std::clamp(f.seg + delta, -1, row.seg_count - 1);
    // Snapshot unconditionally on every horizontal landing: skipping it (e.g.
    // entering cell 0 from the name column) leaves a stale accepted value and
    // the edit marker (*) lights up before any editing started. The call
    // itself is safe on the label column — snapshot_cell() no-ops there.
    snapshot_cell();
}

void MenuEngine::move_cursor(int delta) {
    if (is_rows()) {
        move_row(delta);
        return;
    }
    MenuFrame& f = stack_[depth_ - 1];
    if (f.item_count == 0) {
        return;
    }
    const int max_i = f.item_count - 1;
    const int new_cur = std::clamp(f.cursor + delta, 0, max_i);
    if (new_cur < f.offset) {
        f.offset = new_cur;
    } else if (new_cur >= f.offset + 4) {
        f.offset = new_cur - 3;
    }
    f.cursor = new_cur;
    snapshot_focused_item();
}

void MenuEngine::snapshot_focused_item() {
    const MenuItem* item = current_item();
    if (item == nullptr) {
        return;
    }
    if (item->type == MenuItemType::NoteRange) {
        if (item->get_min && item->get_max) {
            item_snapshot_ = item->get_min();
            cell_accepted_ = item->get_max();
        }
        return;
    }
    if (item->get_i) {
        item_snapshot_ = item->get_i();
    }
}

void MenuEngine::snapshot_range() {
    const MenuItem* item = current_item();
    if (item == nullptr) {
        return;
    }
    if (item->get_min) {
        range_snap_min_ = item->get_min();
    }
    if (item->get_max) {
        range_snap_max_ = item->get_max();
    }
}

void MenuEngine::snapshot_cell() {
    const Segment* seg = current_segment();
    if (seg == nullptr) {
        return;
    }
    if (seg->type == SegmentType::Range) {
        if (seg->get_min) { cell_range_min_ = seg->get_min(); }
        if (seg->get_max) { cell_range_max_ = seg->get_max(); }
        return;
    }
    if (seg->get) {
        cell_accepted_ = seg->get();
    }
}

bool MenuEngine::quick_edited() const {
    if (depth_ == 0 || !is_rows()) {
        return false;
    }
    const Segment* seg = current_segment();
    return seg != nullptr && seg->get && seg->get() != cell_accepted_;
}

void MenuEngine::enter_edit(const Segment& seg) {
    edit_mode_ = true;
    edit_snapshot_ = seg.get ? seg.get() : 0;
}

void MenuEngine::press_short() {
    if (is_rows()) {
        const Segment* seg = current_segment();
        const MenuFrame& f = current();
        // Header zone: a click cycles the play mode KB -> RND -> PTRN -> KB.
        if (f.header_focus) {
            switch (st_->runtime.mode) {
                case PlayMode::MidiKeyboard:
                    st_->runtime.mode = PlayMode::RandomNote;
                    break;
                case PlayMode::RandomNote:
                    st_->runtime.mode = PlayMode::RandomPattern;
                    break;
                default:
                    st_->runtime.mode = PlayMode::MidiKeyboard;
                    break;
            }
            return;
        }
        if (edit_mode_) {
            edit_mode_ = false;
            edit_snapshot_ = 0;
            snapshot_cell();  // the live value becomes the accepted one
            return;
        }
        if (seg == nullptr) {
            if (f.cursor >= 0 && f.cursor < f.row_count) {
                const QuickRow& row = f.rows[f.cursor];
                // The ALL row doubles as a shortcut into the FULL menu root.
                if (std::strcmp(row.label, "ALL") == 0 &&
                    st_->runtime.screen_mode == ScreenMode::Quick) {
                    st_->runtime.screen_mode = ScreenMode::Full;
                    rebuild();
                    return;
                }
                if (row.submenu_count > 0) {
                    push_items(row.submenu, row.submenu_count);
                }
            }
            return;
        }
        if (seg->type == SegmentType::Param) {
            if (seg->child_count > 0) {
                push_items(seg->children, seg->child_count);
            }
            return;
        }
        if (seg->type == SegmentType::Toggle) {
            // A click is the confirmation: flip the value immediately.
            if (seg->set) {
                seg->set(seg->get ? (seg->get() ? 0 : 1) : 0);
                snapshot_cell();
            }
            return;
        }
        if (seg->type == SegmentType::Linear || seg->type == SegmentType::Radial ||
            seg->type == SegmentType::Range) {
            enter_edit(*seg);
        }
        return;
    }

    const MenuItem* item = current_item();
    if (item == nullptr) {
        return;
    }
    if (item->type == MenuItemType::NoteRange) {
        // Click toggles the 2D range editor. A click while editing confirms
        // the live min/max and leaves the active state.
        if (range_edit_) {
            range_edit_ = false;
        } else {
            snapshot_range();
            range_edit_ = true;
        }
        return;
    }
    if (item->type == MenuItemType::Section || item->type == MenuItemType::Group) {
        if (item->child_count > 0) {
            push_items(item->children, item->child_count);
        }
    } else if (item->type == MenuItemType::Action) {
        if (item->action != nullptr) {
            item->action();
        }
    }
}

void MenuEngine::press_long() {
    if (depth_ > 1) {
        pop();
        return;
    }
    // Three interactive screens cycled by hand: QUICK <-> MAIN <-> EDIT.
    // Animation left the cycle: it starts on idle (screensaver) or manually
    // from FULL -> System -> Anim, and any input returns to the origin screen.
    ScreenMode next;
    switch (st_->runtime.screen_mode) {
        case ScreenMode::Quick: next = ScreenMode::Full; break;
        case ScreenMode::Full: next = ScreenMode::Edit; break;
        default: next = ScreenMode::Quick; break;  // Edit / Animation / ...
    }
    st_->runtime.screen_mode = next;
    rebuild();
}

void MenuEngine::reset_value() {
    if (is_rows()) {
        // Restore the focused cell to its last accepted value and leave edit
        // mode. Works both during editing (undo the live tweak) and after the
        // cell was confirmed.
        const Segment* seg = current_segment();
        if (seg != nullptr && seg->type == SegmentType::Range) {
            if (seg->set_min) { seg->set_min(cell_range_min_); }
            if (seg->set_max) { seg->set_max(cell_range_max_); }
            edit_mode_ = false;
            edit_snapshot_ = 0;
            return;
        }
        if (seg != nullptr && seg->set) {
            seg->set(cell_accepted_);
        }
        edit_mode_ = false;
        edit_snapshot_ = 0;
        return;
    }
    const MenuItem* item = current_item();
    if (item == nullptr) {
        return;
    }
    if (item->type == MenuItemType::NoteRange) {
        // Rest + click (or double-click) restores the whole min..max span to
        // the last accepted range and leaves the 2D editor.
        if (item->set_min) { item->set_min(range_snap_min_); }
        if (item->set_max) { item->set_max(range_snap_max_); }
        range_edit_ = false;
        return;
    }
    if (item->set_i) {
        item->set_i(item_snapshot_);
    }
}

void MenuEngine::item_adjust(int delta) {
    const MenuItem* item = current_item();
    if (item == nullptr) {
        return;
    }
    switch (item->type) {
        case MenuItemType::Toggle:
            if (item->set_i) { item->set_i(item->get_i() ? 0 : 1); }
            break;
        case MenuItemType::Option: {
            const int cur = static_cast<int>(item->get_i());
            const int next = std::clamp(cur + delta, 0, item->option_count - 1);
            if (item->set_i) { item->set_i(next); }
            break;
        }
        case MenuItemType::IntSlider: {
            const int32_t step = item->step > 0 ? item->step : 1;
            const int32_t next = std::clamp(item->get_i() + delta * step,
                                            item->min_v, item->max_v);
            if (item->set_i) { item->set_i(next); }
            break;
        }
        case MenuItemType::Rate: {
            const bool note = (item->unit_get && item->unit_get() == 0);
            if (note) {
                const int next = std::clamp(static_cast<int>(item->get_i()) + delta, 0, item->option_count - 1);
                if (item->set_i) { item->set_i(next); }
            } else {
                const int32_t next = std::clamp(item->get_i() + delta * item->step, item->min_v, item->max_v);
                if (item->set_i) { item->set_i(next); }
            }
            break;
        }
        default:
            break;
    }
}

void MenuEngine::item_jump(bool to_min) {
    const MenuItem* item = current_item();
    if (item == nullptr) {
        return;
    }
    switch (item->type) {
        case MenuItemType::Option:
            if (item->set_i) { item->set_i(to_min ? 0 : item->option_count - 1); }
            break;
        case MenuItemType::IntSlider:
            if (item->set_i) { item->set_i(to_min ? item->min_v : item->max_v); }
            break;
        case MenuItemType::Rate: {
            const bool note = (item->unit_get && item->unit_get() == 0);
            if (note) {
                if (item->set_i) { item->set_i(to_min ? 0 : item->option_count - 1); }
            } else {
                if (item->set_i) { item->set_i(to_min ? item->min_v : item->max_v); }
            }
            break;
        }
        default:
            break;
    }
}

void MenuEngine::tilt(Direction dir, bool shift) {
    if (is_rows()) {
        const Segment* seg = current_segment();
        if (edit_mode_) {
            if (seg != nullptr && seg->type == SegmentType::Range) {
                // Range segment: up/down = max bound, left/right = centre shift
                // (width kept), mirroring the DETAIL NoteRange control. Clamps
                // come from the segment bounds (notes by default, index span
                // for the length-range cell).
                const int32_t b_lo = seg->bound_lo;
                const int32_t b_hi = seg->bound_hi;
                int32_t lo = seg->get_min ? seg->get_min() : 0;
                int32_t hi = seg->get_max ? seg->get_max() : 0;
                if (lo == hi) {
                    // Pinned value: left/right drags the point, up/down widens
                    // or narrows the span. The leading edge is applied first so
                    // cross-clamping inside the setters never misorders them.
                    int32_t n_lo = lo;
                    int32_t n_hi = hi;
                    bool changed = false;
                    if (dir == Direction::Up && n_hi < b_hi) {
                        ++n_hi;
                        changed = true;
                    } else if (dir == Direction::Down && n_hi > n_lo) {
                        --n_hi;
                        changed = true;
                    } else if (dir == Direction::Right && n_hi < b_hi) {
                        ++n_lo;
                        ++n_hi;
                        changed = true;
                    } else if (dir == Direction::Left && n_lo > b_lo) {
                        --n_lo;
                        --n_hi;
                        changed = true;
                    }
                    if (changed) {
                        const bool grow_first =
                            (dir == Direction::Right || dir == Direction::Up);
                        if (grow_first) {
                            if (seg->set_max) { seg->set_max(n_hi); }
                            if (seg->set_min) { seg->set_min(n_lo); }
                        } else {
                            if (seg->set_min) { seg->set_min(n_lo); }
                            if (seg->set_max) { seg->set_max(n_hi); }
                        }
                    }
                } else {
                    if (dir == Direction::Up) {
                        hi = std::min(b_hi, hi + 1);
                    } else if (dir == Direction::Down) {
                        hi = std::max(lo, hi - 1);
                    } else if (dir == Direction::Right) {
                        if (hi < b_hi) { ++lo; ++hi; }
                    } else if (dir == Direction::Left) {
                        if (lo > b_lo) { --lo; --hi; }
                    }
                    // Apply the extending edge first: set_min clamps against the
                    // stored max, so writing it before max grew would stall the
                    // shift and widen the span instead of moving it.
                    const bool grow_first =
                        (dir == Direction::Right || dir == Direction::Up);
                    if (grow_first) {
                        if (seg->set_max) { seg->set_max(hi); }
                        if (seg->set_min) { seg->set_min(lo); }
                    } else {
                        if (seg->set_min) { seg->set_min(lo); }
                        if (seg->set_max) { seg->set_max(hi); }
                    }
                }
            } else if (seg != nullptr && seg->type == SegmentType::Linear
                && (dir == Direction::Left || dir == Direction::Right)) {
                const int delta = (dir == Direction::Right) ? 1 : -1;
                if (shift) {
                    const int extreme = (dir == Direction::Left) ? 0 : seg->count - 1;
                    if (seg->set) { seg->set(extreme); }
                } else if (seg->set) {
                    const int next = std::clamp(static_cast<int>(seg->get()) + delta, 0, seg->count - 1);
                    seg->set(next);
                }
            }
            return;
        }

        if (dir == Direction::Up || dir == Direction::Down) {
            move_row((dir == Direction::Down) ? 1 : -1);
            return;
        }
        if (dir == Direction::Left || dir == Direction::Right) {
            if (seg != nullptr && seg->direct) {
                const int delta = (dir == Direction::Right) ? 1 : -1;
                if (shift) {
                    const int extreme = (dir == Direction::Left) ? 0 : seg->count - 1;
                    if (seg->set) { seg->set(extreme); }
                } else if (seg->set) {
                    const int next = std::clamp(static_cast<int>(seg->get()) + delta, 0, seg->count - 1);
                    seg->set(next);
                }
                snapshot_cell();
            } else {
                move_seg((dir == Direction::Right) ? 1 : -1);
            }
            return;
        }
        return;
    }

    const MenuItem* item = current_item();
    if (item == nullptr) {
        return;
    }
    // Active 2D range editing: up/down changes only the MAX (upper) bound,
    // left/right shifts the whole span (centre moves, width is kept). Cursor
    // does not move while a range is edited.
    if (range_edit_ && item->type == MenuItemType::NoteRange && item->set_min && item->set_max) {
        // Bounds come from the item itself (MIDI notes for NT_RNG, division
        // indices for the Len Range item).
        const int32_t b_lo = item->min_v;
        const int32_t b_hi = item->max_v;
        int32_t lo = item->get_min ? item->get_min() : 0;
        int32_t hi = item->get_max ? item->get_max() : 0;
        if (dir == Direction::Up) {
            hi = std::min(b_hi, hi + 1);
        } else if (dir == Direction::Down) {
            hi = std::max(lo, hi - 1);
        } else if (dir == Direction::Right) {
            if (hi < b_hi) { ++lo; ++hi; }
        } else if (dir == Direction::Left) {
            if (lo > b_lo) { --lo; --hi; }
        }
        // Extending edge first, so cross-clamping never stalls a shift.
        if (dir == Direction::Right || dir == Direction::Up) {
            item->set_max(hi);
            item->set_min(lo);
        } else {
            item->set_min(lo);
            item->set_max(hi);
        }
        return;
    }
    if (item->type == MenuItemType::Section || item->type == MenuItemType::Group) {
        if (dir == Direction::Up || dir == Direction::Down) {
            move_cursor((dir == Direction::Down) ? 1 : -1);
        }
        return;
    }
    if (dir == Direction::Up || dir == Direction::Down) {
        move_cursor((dir == Direction::Down) ? 1 : -1);
        return;
    }
    if (dir == Direction::Left) {
        if (shift) { item_jump(true); } else { item_adjust(-1); }
    } else if (dir == Direction::Right) {
        if (shift) { item_jump(false); } else { item_adjust(1); }
    }
}

void MenuEngine::radial_select(int zone) {
    if (is_rows()) {
        const Segment* seg = current_segment();
        if (seg != nullptr && seg->type == SegmentType::Radial && edit_mode_ && seg->set) {
            seg->set(zone);
        }
        return;
    }
    const MenuItem* item = current_item();
    if (item == nullptr || item->type != MenuItemType::Option) {
        return;
    }
    if (zone >= 0 && zone < item->option_count && item->set_i) {
        item->set_i(zone);
    }
}

}  // namespace drom