#include "menu_engine.hpp"

#include <algorithm>

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
        case ScreenMode::Animation:
        default:
            break;  // no editable content
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
    // Header zone (mode switch): only the up/down move is meaningful — Up is a
    // no-op (already at the top), Down returns to row 0's left column.
    if (f.header_focus) {
        if (delta > 0) {
            f.header_focus = false;
            f.cursor = 0;
            f.seg = -1;
            snapshot_cell();
        }
        return;
    }
    // Keep the same column when moving vertically: moving down from the 2nd
    // segment of a row lands on the 2nd segment of the row below (clamped to
    // the target row's segment count). Reaching row 0's left column (seg == -1)
    // and tilting up moves the focus onto the header zone instead.
    const int old_seg = f.seg;
    const int max_i = f.row_count - 1;
    const int new_cur = std::clamp(f.cursor + delta, 0, max_i);
    if (delta < 0 && f.cursor == 0 && f.seg == -1) {
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
    // row 0 and continues from its left column.
    if (f.header_focus) {
        f.header_focus = false;
        f.cursor = 0;
        f.seg = -1;
    }
    const Segment* seg = current_segment();
    const QuickRow& row = f.rows[f.cursor];
    // -1 is the row caption column; moving left from cell 0 lands on it when
    // the row carries a DETAIL submenu (seg = nullptr triggers the push).
    f.seg = std::clamp(f.seg + delta, -1, row.seg_count - 1);
    if (seg != nullptr || f.seg == -1) {
        snapshot_cell();
    }
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
        // Header zone: a click toggles the play mode (KB <-> RND).
        if (f.header_focus) {
            st_->runtime.mode = (st_->runtime.mode == PlayMode::RandomNote)
                                    ? PlayMode::MidiKeyboard
                                    : PlayMode::RandomNote;
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
    ScreenMode next = ScreenMode::Quick;
    switch (st_->runtime.screen_mode) {
        case ScreenMode::Quick: next = ScreenMode::Full; break;
        case ScreenMode::Full: next = ScreenMode::Animation; break;
        case ScreenMode::Animation: next = ScreenMode::Quick; break;
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
                // Note-range segment: up/down = max bound, left/right = centre
                // shift (width kept), mirroring the DETAIL NoteRange control.
                int32_t lo = seg->get_min ? seg->get_min() : 0;
                int32_t hi = seg->get_max ? seg->get_max() : 0;
                if (dir == Direction::Up) {
                    hi = std::min(static_cast<int32_t>(kNoteRangeMax), hi + 1);
                } else if (dir == Direction::Down) {
                    hi = std::max(lo, hi - 1);
                } else if (dir == Direction::Right) {
                    if (hi < kNoteRangeMax) { ++lo; ++hi; }
                } else if (dir == Direction::Left) {
                    if (lo > kNoteRangeMin) { --lo; --hi; }
                }
                if (seg->set_min) { seg->set_min(lo); }
                if (seg->set_max) { seg->set_max(hi); }
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
        int32_t lo = item->get_min ? item->get_min() : 0;
        int32_t hi = item->get_max ? item->get_max() : 0;
        if (dir == Direction::Up) {
            hi = std::min(static_cast<int32_t>(kNoteRangeMax), hi + 1);
        } else if (dir == Direction::Down) {
            hi = std::max(lo, hi - 1);
        } else if (dir == Direction::Right) {
            if (hi < kNoteRangeMax) { ++lo; ++hi; }
        } else if (dir == Direction::Left) {
            if (lo > kNoteRangeMin) { --lo; --hi; }
        }
        item->set_min(lo);
        item->set_max(hi);
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