#include "renderer.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "../engine/midi_chain.hpp"

namespace drom {

namespace {

constexpr int kStatusY = 0;
constexpr int kRowH = 8;
constexpr int kContentY = 8;
constexpr int kMaxCols = 21;  // 128px / 6px per glyph

// QUICK 3-column cell grid: row label on the left (3-glyph names keep the
// gutter at 18px), then three fixed columns so the cells of Key/Chord/Arp line
// up vertically and never shift when a value or the edit marker changes.
// col0 starts close to the gutter so a 7-glyph pitch range ("F#2:D#4") clears
// the LEN column.
constexpr int kLabelW = 18;
constexpr int kColX[3] = {20, 62, 98};
constexpr int kCellW = 32;
constexpr int kCellPad = 2;

// Status-bar "infographics": play triangle + rec dot + quarter-note icon + the
// beat meter on the right. The mode name is drawn at x=0; the transport icons
// live in the gutter right after it (x=36/43/46), so the octave/bpm/step meta
// must start AFTER the icons.
constexpr int kPlayIconX = 34;
constexpr int kRecIconX = 43;
constexpr int kNoteIconX = 48;  // quarter-note icon, right of the rec dot
constexpr int kStatusMetaX = 56;
constexpr int kIconY = 0;
constexpr int kIconSize = 5;
constexpr int kProgressY = 7;  // thin blank row between status and content

// Beat circle: a 7x7 dial with four quarter sectors placed just left of the
// BPM value. While a pattern is running (`beat >= 0`) the sector of the current
// quarter is filled; otherwise only the 1px outline is drawn (hollow dial).
constexpr int kBeatDialSize = 7;   // 7x7 px, centre (3,3)
constexpr int kBeatRadius = 3;     // max |dx|/|dy| from the centre
constexpr int kBeatDialY = 0;      // top-left y of the dial (same row as meta)
constexpr int kBeatGap = 2;        // px between the dial and the BPM text

// QUICK screen caption line: names the columns of the focused row so it is
// always clear which parameter each cell edits (fits inside 128px width).
constexpr int kCaptionY = 8;
constexpr int kCaptionRowH = 8;

// DETAIL/MAIN NoteRange: a thin band under the mini-range tracks the active
// min..max over the full 128px strip (left = note 12 / C0, right = 119 / B8).
constexpr int kRangeBarY = 7;  // one pixel above the next row

void draw_play_icon(DisplaySh1106& d) {
    // Right-pointing triangle (classic play symbol): full height on the left,
    // one pixel apex on the right.
    for (int dx = 0; dx < kIconSize; ++dx) {
        const int h = kIconSize - dx;  // 5,4,3,2,1
        d.fill_rect(kPlayIconX + dx, kIconY + (kIconSize - h) / 2, 1, h, true);
    }
}

void draw_rec_icon(DisplaySh1106& d) {
    // Filled circle (classic REC dot): every pixel within the disc radius.
    const int cx = kRecIconX + kIconSize / 2;
    const int cy = kIconY + kIconSize / 2;
    const int r = kIconSize / 2;
    for (int dy = -r; dy <= r; ++dy) {
        for (int dx = -r; dx <= r; ++dx) {
            if (dx * dx + dy * dy <= r * r) {
                d.set_pixel(cx + dx, cy + dy, true);
            }
        }
    }
}

void draw_note_icon(DisplaySh1106& d) {
    // Quarter-note: vertical stem, flag, filled head (6 px tall).
    d.fill_rect(kNoteIconX + 2, kIconY, 1, 6, true);          // stem
    d.fill_rect(kNoteIconX + 3, kIconY, 2, 1, true);          // flag
    d.fill_rect(kNoteIconX, kIconY + 3, 3, 3, true);          // head
}

// Clockwise sectors from the dial centre: 0 top / 1 right / 2 bottom / 3 left.
// A pixel belongs to the sector of the dominant axis (ties resolve clockwise).
int beat_sector(int dx, int dy) {
    const int ax = dx < 0 ? -dx : dx;
    const int ay = dy < 0 ? -dy : dy;
    if (ax >= ay) {
        return dx >= 0 ? 1 : 3;
    }
    return dy >= 0 ? 2 : 0;
}

void draw_beat_dial(DisplaySh1106& d, int x, int beat) {
    const int cx = x + kBeatRadius;
    const int cy = kBeatDialY + kBeatRadius;
    for (int dy = -kBeatRadius; dy <= kBeatRadius; ++dy) {
        for (int dx = -kBeatRadius; dx <= kBeatRadius; ++dx) {
            // Disc mask: pixels within the radius circle (plus diagonal fill).
            const bool inside = dx * dx + dy * dy <= kBeatRadius * kBeatRadius + 2;
            if (!inside) {
                continue;
            }
            // 1px outline: any disc pixel with a missing 4-neighbour.
            const bool on_ring = (dx == -kBeatRadius || dx == kBeatRadius || dy == -kBeatRadius ||
                                  dy == kBeatRadius);
            if (on_ring) {
                d.set_pixel(cx + dx, cy + dy, true);
            } else if (beat >= 0 && beat_sector(dx, dy) == beat) {
                d.set_pixel(cx + dx, cy + dy, true);
            }
        }
    }
}

const char* screen_prefix(ScreenMode m) {
    switch (m) {
        case ScreenMode::Quick: return "Quick";
        case ScreenMode::Full: return "Menu";
        case ScreenMode::Edit: return "Edit";
        case ScreenMode::Animation: return "Anim";
    }
    return "?";
}

const char* midi_note_glyph(uint8_t midi) {
    static const char* kGlyphNames[12] = {"C", "C#", "D", "D#", "E", "F",
                                          "F#", "G", "G#", "A", "A#", "B"};
    return kGlyphNames[midi % 12];
}

int midi_note_octave(uint8_t midi) {
    return static_cast<int>(midi / 12) - 1;
}

void seg_value_text(const Segment& s, char* out, int cap) {
    if (s.type == SegmentType::Range) {
        // Span string from the live min/max accessors. min == max means the
        // value is pinned — print it once ("'4", not "'4-'4").
        const int32_t lo = s.get_min ? s.get_min() : 0;
        const int32_t hi = s.get_max ? s.get_max() : 0;
        if (s.label_fn) {
            const char* lo_txt = s.label_fn(lo);
            if (lo_txt != nullptr && lo == hi) {
                snprintf(out, cap, "%s", lo_txt);
                return;
            }
            const char* hi_txt = s.label_fn(hi);
            if (lo_txt != nullptr && hi_txt != nullptr) {
                snprintf(out, cap, "%s-%s", lo_txt, hi_txt);
                return;
            }
        }
        // Colon between the notes ("F#2:D#4") — compact, yet readable; the
        // cell still clears the neighbouring LEN column.
        if (lo == hi) {
            snprintf(out, cap, "%s%d",
                     midi_note_glyph(static_cast<uint8_t>(lo)),
                     midi_note_octave(static_cast<uint8_t>(lo)));
            return;
        }
        snprintf(out, cap, "%s%d:%s%d",
                 midi_note_glyph(static_cast<uint8_t>(lo)),
                 midi_note_octave(static_cast<uint8_t>(lo)),
                 midi_note_glyph(static_cast<uint8_t>(hi)),
                 midi_note_octave(static_cast<uint8_t>(hi)));
        return;
    }
    const int v = static_cast<int>(s.get());
    if (s.label_fn) {
        const char* txt = s.label_fn(v);
        if (txt != nullptr) {
            snprintf(out, cap, "%s", txt);
            return;
        }
    }
    if (s.labels != nullptr && v >= 0 && v < s.count) {
        snprintf(out, cap, "%s", s.labels[v]);
    } else {
        snprintf(out, cap, "%d", v);
    }
}

// "C3", or in RandomNote the pressed key -> generated note ("C3>F#4").
void format_note_txt(const AppState& state, char* out, int cap) {
    if (!state.runtime.show_note) {
        out[0] = '\0';
        return;
    }
    if (state.runtime.mode == PlayMode::RandomNote &&
        state.runtime.last_input_note != 0) {
        snprintf(out, cap, "%s%d>%s%d",
                 midi_note_glyph(state.runtime.last_input_note),
                 midi_note_octave(state.runtime.last_input_note),
                 midi_note_glyph(state.runtime.last_note),
                 midi_note_octave(state.runtime.last_note));
    } else {
        snprintf(out, cap, "%s%d",
                 midi_note_glyph(state.runtime.last_note),
                 midi_note_octave(state.runtime.last_note));
    }
}

void item_value_text(const MenuItem& it, char* out, int cap) {
    switch (it.type) {
        case MenuItemType::Toggle:
            snprintf(out, cap, "%s", it.get_i() ? "On" : "Off");
            break;
        case MenuItemType::Option: {
            const int v = static_cast<int>(it.get_i());
            if (it.label_fn) {
                const char* s = it.label_fn(v);
                snprintf(out, cap, "%s", s ? s : "?");
            } else if (it.option_labels != nullptr && v >= 0 && v < it.option_count) {
                snprintf(out, cap, "%s", it.option_labels[v]);
            } else {
                snprintf(out, cap, "%d", v);
            }
            break;
        }
        case MenuItemType::IntSlider:
            snprintf(out, cap, "%d", static_cast<int>(it.get_i()));
            break;
        case MenuItemType::Rate: {
            const int v = static_cast<int>(it.get_i());
            if (it.unit_get && it.unit_get() == 0 && v >= 0 && v < kArpNoteDivCount) {
                snprintf(out, cap, "%s", kArpNoteDivs[v]);
            } else {
                snprintf(out, cap, "%dms", v);
            }
            break;
        }
        case MenuItemType::NoteRange:
            // "C3..B6" for notes, or the item's dynamic labels ("16..'2" for
            // the length range). min == max prints a single value.
            if (it.label_fn) {
                const int32_t lo = it.get_min ? it.get_min() : 0;
                const int32_t hi = it.get_max ? it.get_max() : 0;
                const char* lo_txt = it.label_fn(lo);
                if (lo_txt != nullptr && lo == hi) {
                    snprintf(out, cap, "%s", lo_txt);
                    break;
                }
                const char* hi_txt = it.label_fn(hi);
                if (lo_txt != nullptr && hi_txt != nullptr) {
                    snprintf(out, cap, "%s..%s", lo_txt, hi_txt);
                    break;
                }
            }
            // "C3..B6": min on the left, max on the right, `#` strip between.
            snprintf(out, cap, "%s%d..%s%d",
                     midi_note_glyph(static_cast<uint8_t>(it.get_min())),
                     midi_note_octave(static_cast<uint8_t>(it.get_min())),
                     midi_note_glyph(static_cast<uint8_t>(it.get_max())),
                     midi_note_octave(static_cast<uint8_t>(it.get_max())));
            break;
        default:
            out[0] = '\0';
            break;
    }
}

void draw_quick_cell(DisplaySh1106& d, int col, int y, const char* text, bool inverted,
                     bool edited) {
    const int x = kColX[col];
    if (inverted) {
        // The highlight sits one pixel above the text row so the letters look
        // vertically centered inside the fill; the text itself never moves.
        // The fill widens past the fixed 32px cell to cover the whole printed
        // text: range cells like "F#2-D#4" (8 glyphs = 48px) and long caption
        // labels overflow the fixed column, and their tail glyphs must stay
        // inside the inverted band to remain visible.
        const int text_w = static_cast<int>(std::strlen(text)) * DisplaySh1106::kTextAdvance;
        int w = kCellW;
        if (text_w + kCellPad > w) {
            w = text_w + kCellPad;
        }
        if (x + w > DisplaySh1106::kWidth) {
            w = DisplaySh1106::kWidth - x;
        }
        d.fill_rect(x, y - 1, w, kRowH, true);
        d.draw_text_px(text, x + kCellPad, y, false);
    } else {
        d.draw_text(text, x + kCellPad, y);
    }
    if (edited) {
        // `*` marker in the last glyph slot of the cell, inverted to match the
        // surrounding fill when the cell is highlighted.
        const bool on = !inverted;
        const char mark = '*';
        d.draw_text_px(&mark, x + kCellW - DisplaySh1106::kTextAdvance, y, on);
    }
}

// NoteRange overview strip (DETAIL / MAIN items): a thin band under the row
// spanning the active min..max across the display. Focused rows get a heavier
// underline. The QUICK Range cell draws the same strip under the row label.
void draw_range_band(DisplaySh1106& d, const MenuItem& it, int y, bool focused) {
    if (!it.get_min || !it.get_max) {
        return;
    }
    const int32_t lo = it.get_min();
    const int32_t hi = it.get_max();
    // Scale the band over the item's own editable span (MIDI notes by default,
    // division indices for the length-range item).
    const int32_t b_lo = it.min_v;
    const int32_t span = (it.max_v - b_lo);
    int x = (span <= 0) ? 0
                        : (static_cast<int>(lo) - b_lo) * DisplaySh1106::kWidth / span;
    int w = (span <= 0) ? DisplaySh1106::kWidth
                        : (static_cast<int>(hi) - b_lo) * DisplaySh1106::kWidth / span - x;
    if (w < 1) {
        w = 1;
    }
    // Clip right edge.
    if (x + w > DisplaySh1106::kWidth) {
        w = DisplaySh1106::kWidth - x;
    }
    if (w <= 0) {
        return;
    }
    const int band_y = y + kRangeBarY;
    d.fill_rect(x, band_y, w, focused ? 2 : 1, true);
}

void draw_seg_range_band(DisplaySh1106& d, const Segment& s, int y, bool focused) {
    if (!s.get_min || !s.get_max) {
        return;
    }
    const int32_t lo = s.get_min();
    const int32_t hi = s.get_max();
    // Scale the band over the segment's own bounds (MIDI notes by default,
    // division indices for the length-range cell).
    const int32_t b_lo = s.bound_lo;
    const int32_t span = (s.bound_hi - b_lo);
    int x = (span <= 0) ? 0
                        : (static_cast<int>(lo) - b_lo) * DisplaySh1106::kWidth / span;
    int w = (span <= 0) ? DisplaySh1106::kWidth
                        : (static_cast<int>(hi) - b_lo) * DisplaySh1106::kWidth / span - x;
    if (w < 1) {
        w = 1;
    }
    if (x + w > DisplaySh1106::kWidth) {
        w = DisplaySh1106::kWidth - x;
    }
    if (w <= 0) {
        return;
    }
    d.fill_rect(x, y + 7, w, focused ? 2 : 1, true);
}

// Pattern editor screen (ScreenMode::Edit): a piano-roll of the visible page
// (16 steps across the full width) with auto-scaled pitch rows, plus a single
// detail line: STEP, NOTE, LEN, ON and the pattern length (PLEN).
void draw_pattern_editor(const AppState& state, DisplaySh1106& d) {
    const Pattern& p = state.active_pattern();
    const PtnEditorUI& ed = state.editor;
    const int len = std::max<int>(1, std::min<int>(p.length, kStepCountMax));
    const int page_len =
        std::min(16, len - static_cast<int>(ed.page) * 16);
    char buf[kMaxCols + 1];

    // -- Pitch axis auto-scale: only the rows actually used by the pattern are
    // visible; the window grows when a new note appears outside it.
    int lo = 127;
    int hi = 0;
    bool any = false;
    for (int i = 0; i < len; ++i) {
        const Step& st = p.steps[i];
        if (st.active && st.note_count > 0) {
            any = true;
            lo = std::min(lo, static_cast<int>(st.notes[0]));
            hi = std::max(hi, static_cast<int>(st.notes[0]));
        }
    }
    if (!any) {
        lo = 55;
        hi = 67;  // empty pattern: a one-octave window around C4
    }
    lo -= 1;
    hi += 1;
    const int span = std::max(1, hi - lo);

    // -- Roll geometry: 8 px per step x 16 columns = full display width.
    const int col_w = DisplaySh1106::kWidth / 16;   // 8
    const int roll_top = kContentY + 1;
    const int roll_h = 36;  // taller note area; detail line still fits at row 62
    const int floor_y = roll_top + roll_h - 1;

    // Beat grid: faint ticks every 4 steps, stronger on page starts.
    for (int c = 0; c < page_len; ++c) {
        const int gx = c * col_w;
        if ((ed.page * 16 + c) % 4 == 0) {
            d.fill_rect(gx, floor_y - 3, 1, 3, true);
        }
    }
    // Floor line.
    d.fill_rect(0, floor_y, page_len * col_w, 1, true);

    // Notes: a dot at the pitch row (relative position preserved), with a
    // short duration tail to the right proportional to the LEN division. Kept
    // as pure dots/blocks so no vertical bars ever cover the note field.
    for (int c = 0; c < page_len; ++c) {
        const Step& s = p.steps[ed.page * 16 + c];
        if (!s.active || s.note_count == 0) {
            continue;
        }
        const int rel = std::clamp(static_cast<int>(s.notes[0]) - lo, 0, span);
        const int y = roll_top + (span - rel) * (roll_h - 2) / span;
        const int x = c * col_w + 2;
        // Note head: 3x3 block (readable at a glance).
        d.fill_rect(x, y - 1, 3, 3, true);
        // Duration tail inside the column (division index scales it).
        const int tail = std::min(col_w - 3,
                                  1 + static_cast<int>(s.len_div) / 2);
        if (tail > 1) {
            d.fill_rect(x + 3, y, tail, 1, true);
        }
    }

    // Cursor: a small tick on the top edge and a dot under the floor line —
    // nothing covering the note rows themselves. This column is ALSO the
    // playback/playhead column (see below), so there is a single unified
    // "current step" marker; editing happens exactly where playback stops.
    {
        const int cx = std::min<int>(ed.cur % 16, page_len - 1) * col_w;
        d.fill_rect(cx, roll_top - 1, col_w, 2, true);   // top tick
        d.fill_rect(cx + col_w / 2 - 1, floor_y + 1, 2, 1, true); // under-dot
    }
    // Playback: a thin vertical bar on the left edge of the current column
    // sweeps across the roll as the loop advances (Play started it). This is
    // the same column as the cursor: when Play stops the bar freezes on the
    // editable step, so the playhead, the cursor and the edit target are one.
    {
        int bar_step =
            std::min<int>(state.runtime.current_step, len - 1);
        if (static_cast<int>(bar_step / 16) != static_cast<int>(ed.page)) {
            bar_step = static_cast<int>(ed.page) * 16;
        }
        const int bx = (bar_step % 16) * col_w;
        d.fill_rect(bx, roll_top, 1, roll_h, true);  // moving vertical marker
    }

    // Range selection: a marked inclusive step range inverts the whole height
    // of the roll over the selected columns (clipped to the visible page), so
    // it is unmistakable which area is selected. It is a separate concern from
    // the cursor/playhead marker, so it never moves the edit target.
    {
        int sel_lo = 0, sel_hi = 0;
        if (selection_bounds(ed, len, sel_lo, sel_hi)) {
            int first = -1, last = -1;
            for (int c = 0; c < page_len; ++c) {
                const int abs = static_cast<int>(ed.page) * 16 + c;
                if (abs >= sel_lo && abs <= sel_hi) {
                    if (first < 0) first = c;
                    last = c;
                }
            }
            if (first >= 0) {
                const int ux = first * col_w;
                const int uw = (last - first + 1) * col_w;
                d.invert_rect(ux, roll_top, uw,
                              floor_y + 2 - roll_top);  // full roll height
            }
        }
    }


    // -- Detail line: STEP NOTE LEN ON PLEN(page) ----------------------------
    const Step& s = p.steps[std::min<int>(ed.cur, kStepCountMax - 1)];
    const int dy = floor_y + 10;
    const bool is_on = s.active && s.note_count > 0;

    auto field_box = [&](int x, int w, const char* text, bool active_field) {
        if (active_field) {
            d.fill_rect(x, dy - 9, w, kRowH, true);
            d.draw_text_px(text, x + kCellPad, dy - 8, false);
        } else {
            d.draw_text(text, x + kCellPad, dy - 8);
        }
    };

    snprintf(buf, sizeof(buf), "S%02d", ed.page * 16 + std::min<int>(ed.cur % 16, page_len - 1) + 1);
    field_box(0, 26, buf, false);

    char note_buf[8];       // "C4", "F#3", "---" (no +/- sign inside)
    if (is_on) {
        snprintf(note_buf, sizeof(note_buf), "%s%d",
                 midi_note_glyph(s.notes[0]), midi_note_octave(s.notes[0]));
    } else {
        snprintf(note_buf, sizeof(note_buf), "---");
    }
    // The NOTE name is always drawn at the SAME position (field x + pad). The
    // direction signs occupy reserved slots that never move the name: "-" in
    // the slot LEFT of the name when the note went LOWER, "+" in the slot
    // RIGHT when it went HIGHER. The name itself stays in one column whether
    // or not a sign (or a "#" in the name) is present. The status sign stays
    // visible until the change is confirmed (click) or undone (Rest).
    const int note_x = 28;
    const int note_pad_x = note_x + kCellPad;
    const bool note_focused = (ed.field == 0);
    const int16_t orig =
        ed.prev_notes[std::min<std::size_t>(ed.cur, kStepCountMax - 1)];
    const bool pitch_pending = is_on && orig >= 0 &&
                               static_cast<int>(s.notes[0]) != orig;
    if (note_focused) {
        d.fill_rect(note_x, dy - 9, 34, kRowH, true);
        d.draw_text_px(note_buf, note_pad_x, dy - 8, false);
    } else {
        d.draw_text(note_buf, note_pad_x, dy - 8);
    }
    if (pitch_pending) {
        if (static_cast<int>(s.notes[0]) > orig) {
            d.draw_text("+", note_pad_x +
                        static_cast<int>(std::strlen(note_buf)) *
                            DisplaySh1106::kTextAdvance, dy - 8);
        } else {
            d.draw_text("-", note_x, dy - 8);
        }
    }

    snprintf(buf, sizeof(buf), "L%s",
             kNoteLenDivs[std::min<uint8_t>(s.len_div, kNoteLenDivCount - 1)]);
    field_box(62, 24, buf, ed.field == 1);

    field_box(88, 20, is_on ? "ON" : "OFF", ed.field == 2);

    snprintf(buf, sizeof(buf), "P%d", static_cast<int>(p.length));
    field_box(110, 20, buf, ed.field == 3);

    // Page indicator (only when the loop spans more than one page).
    const int pages = (len + 15) / 16;
    if (pages > 1) {
        snprintf(buf, sizeof(buf), "%d/%d",
                 static_cast<int>(ed.page) + 1, pages);
        d.draw_text(buf, DisplaySh1106::kWidth - 18, roll_top - 9);
    }
    // Transient hotkey confirmation ("COPY"/"PASTE"/"UNDO"/"REDO"/"DUP").
    // Drawn at the very BOTTOM row — the top line is reserved for the device
    // parameters, so a Shift+note shortcut shows below the editor instead.
    if (ed.hint != kHintNone) {
        const char* label = "";
        switch (ed.hint) {
            case kHintCopy:   label = "COPY"; break;
            case kHintPaste:  label = "PAST"; break;
            case kHintUndo:   label = "UNDO"; break;
            case kHintRedo:   label = "REDO"; break;
            case kHintDup:    label = "DUP";  break;
            default: break;
        }
        if (*label) {
            d.draw_text(label, 0, DisplaySh1106::kHeight - 10);
        }
    }
    // SELECT indicator at the bottom-right of the editor: "SEL" when a range is
    // marked, "SEL+" while the SELECT sub-mode is active (range being shaped).
    if (ed.sel_active) {
        const char* m = ed.sel_mode ? "SEL+" : "SEL";
        const int w = static_cast<int>(std::strlen(m)) *
                      DisplaySh1106::kTextAdvance + 8;
        d.draw_text(m, DisplaySh1106::kWidth - w, DisplaySh1106::kHeight - 10);
    }
}

}  // namespace

void draw_test_screen(const AppState& state, DisplaySh1106& d) {
    // Full-screen raw-input test: one line per input group with a 1/0 bit for
    // each pressed key. Entered from the FULL "Test" menu item; exit with
    // Shift+one click.
    char buf[kMaxCols + 1];

    // Note keys 1..16 (raw bits 0..15). bit = 1 means pressed.
    const uint16_t notes = state.runtime.note_bits;
    snprintf(buf, sizeof(buf), "N %c%c%c%c%c%c%c%c",
             (notes & (1u << 0)) ? '#' : '.',
             (notes & (1u << 1)) ? '#' : '.',
             (notes & (1u << 2)) ? '#' : '.',
             (notes & (1u << 3)) ? '#' : '.',
             (notes & (1u << 4)) ? '#' : '.',
             (notes & (1u << 5)) ? '#' : '.',
             (notes & (1u << 6)) ? '#' : '.',
             (notes & (1u << 7)) ? '#' : '.');
    d.draw_text(buf, 0, kContentY);

    snprintf(buf, sizeof(buf), "9 %c%c%c%c%c%c%c%c",
             (notes & (1u << 8)) ? '#' : '.',
             (notes & (1u << 9)) ? '#' : '.',
             (notes & (1u << 10)) ? '#' : '.',
             (notes & (1u << 11)) ? '#' : '.',
             (notes & (1u << 12)) ? '#' : '.',
             (notes & (1u << 13)) ? '#' : '.',
             (notes & (1u << 14)) ? '#' : '.',
             (notes & (1u << 15)) ? '#' : '.');
    d.draw_text(buf, 0, kContentY + kRowH);

    // Functional keys: chip3 bits (bit0..bit3 = Play/Rest/Rec/Shift, bit6/7 = OctUp/OctDown).
    const uint8_t f = state.runtime.func_bits;
    snprintf(buf, sizeof(buf), "F P%cR%cC%cS%c+%c-%c",
             (f & (1u << 0)) ? '#' : '.',
             (f & (1u << 1)) ? '#' : '.',
             (f & (1u << 2)) ? '#' : '.',
             (f & (1u << 3)) ? '#' : '.',
             (f & (1u << 6)) ? '#' : '.',
             (f & (1u << 7)) ? '#' : '.');
    d.draw_text(buf, 0, kContentY + 2 * kRowH);

    d.draw_text("<Shift+Click exit>", 0, kContentY + 4 * kRowH);
}

void MenuRenderer::render(const AppState& state, const MenuEngine& engine) {
    display_->clear();

    // Left segment: the screen prefix. On the QUICK screen the word "Quick" is
    // replaced by the active play mode (Keyboard / RandomNote) — the header of
    // the screen doubles as the mode switch. When the header zone is focused the
    // text is inverted, same as a selected cell.
    const char* header = screen_prefix(state.runtime.screen_mode);
    if (state.runtime.screen_mode == ScreenMode::Quick) {
        header = (state.runtime.mode == PlayMode::RandomNote)   ? "RND"
                 : (state.runtime.mode == PlayMode::RandomPattern) ? "PTRN"
                                                                   : "KB";
    }
    const bool hdr_focus = engine.header_focus();
    if (hdr_focus) {
        const int w = static_cast<int>(std::strlen(header)) * DisplaySh1106::kTextAdvance + kCellPad;
        display_->fill_rect(0, kStatusY, w, kRowH, true);
        display_->draw_text_px(header, kCellPad, kStatusY, false);
    } else {
        display_->draw_text(header, 0, kStatusY);
    }

    // Middle segment: octave, then the beat dial left of the BPM value.
    // RandomNote has no pattern step, so only octave + BPM are shown.
    char oct_txt[kMaxCols + 1];
    snprintf(oct_txt, sizeof(oct_txt), "O%d", static_cast<int>(state.runtime.base_octave));
    display_->draw_text(oct_txt, kStatusMetaX, kStatusY);

    const int dial_x =
        kStatusMetaX + static_cast<int>(std::strlen(oct_txt)) * DisplaySh1106::kTextAdvance;
    // Beat dial: hollow (outline only) when not playing, one filled
    // quarter-sector matching the running quarter while it plays.
    if (state.runtime.playing) {
        draw_beat_dial(*display_, dial_x, static_cast<int>(state.runtime.beat));
    } else {
        draw_beat_dial(*display_, dial_x, -1);
    }

    char bpm_txt[kMaxCols + 1];
    snprintf(bpm_txt, sizeof(bpm_txt), "%d", static_cast<int>(state.active_pattern().timing.bpm));
    const int bpm_x = dial_x + kBeatDialSize + kBeatGap;
    display_->draw_text(bpm_txt, bpm_x, kStatusY);

    // Meta right edge for the live-note readout: everything drawn this frame
    // between the octave/dial/BPM (and step in KB modes) and the screen edge.
    int meta_right = bpm_x + static_cast<int>(std::strlen(bpm_txt)) * DisplaySh1106::kTextAdvance;
    if (state.runtime.mode != PlayMode::RandomNote) {
        char step_txt[8];
        snprintf(step_txt, sizeof(step_txt), "S%d", static_cast<int>(state.runtime.current_step));
        const int step_x = bpm_x + static_cast<int>(std::strlen(bpm_txt)) * DisplaySh1106::kTextAdvance;
        if (step_x + 2 * DisplaySh1106::kTextAdvance <= DisplaySh1106::kWidth) {
            display_->draw_text(step_txt, step_x, kStatusY);
            meta_right = step_x + static_cast<int>(std::strlen(step_txt)) * DisplaySh1106::kTextAdvance;
        }
    }

    // Live-note readout in the status line, right after the meta block: in
    // RandomNote it shows the pressed key -> generated note ("C3>F#4"), in KB
    // just the last note ("C3"). Skipped when it would run past the screen edge.
    {
        char note_txt[12] {};
        format_note_txt(state, note_txt, sizeof(note_txt));
        const int nlen = static_cast<int>(std::strlen(note_txt));
        if (nlen > 0) {
            const int note_x = meta_right + 1;
            if (note_x + nlen * DisplaySh1106::kTextAdvance <= DisplaySh1106::kWidth) {
                display_->draw_text(note_txt, note_x, kStatusY);
            }
        }
    }

    char status[kMaxCols + 1];

    // Icon gutter: play triangle / rec dot / quarter-note (cheap fill_rect, no
    // font glyphs).
    if (state.runtime.playing) {
        draw_play_icon(*display_);
    }
    if (state.runtime.recording) {
        draw_rec_icon(*display_);
    }
    draw_note_icon(*display_);

    // NOTE: the thin progress line under the status bar is intentionally not
    // drawn: there is no real step-grid playback yet (GEN/PTRN chains events by
    // duration), so a bar would mislead more than inform.

    if (state.runtime.test_mode) {
        draw_test_screen(state, *display_);
        return;
    }

    if (state.runtime.screen_mode == ScreenMode::Animation) {
        animation_.render(*display_);
        return;
    }

    if (state.runtime.screen_mode == ScreenMode::Edit) {
        draw_pattern_editor(state, *display_);
        return;
    }

    if (engine.depth() == 0) {
        return;
    }

    const MenuFrame& f = engine.current();

    if (engine.is_rows()) {
        // Caption line for the focused row: names each edit cell exactly above
        // its grid column (inverted style, pinned to the top of the list), so it
        // stays on screen while the cursor moves to lower rows. The header (mode
        // switch) has no cells, so it gets the full-width summary style instead.
        const bool hdr_focus = engine.header_focus();
        if (!hdr_focus && f.cursor >= 0 && f.cursor < f.row_count) {
            const QuickRow& prow = f.rows[f.cursor];
            if (prow.seg_count > 0) {
                for (int si = 0; si < prow.seg_count && si < 3; ++si) {
                    const char* lbl = prow.segments[si].label ? prow.segments[si].label : "PRM";
                    draw_quick_cell(*display_, si, kCaptionY, lbl, true, false);
                }
            } else if (prow.label != nullptr) {
                // Summary-only row: full-width inverted line, same style as the row.
                display_->fill_rect(0, kCaptionY - 1, DisplaySh1106::kWidth,
                                    kCaptionRowH, true);
                display_->draw_text_px(prow.label, 0, kCaptionY, false);
            }
        }

        for (int i = 0; i < 6; ++i) {
            const int idx = f.offset + i;
            if (idx < 0 || idx >= f.row_count) {
                break;
            }
            const QuickRow& row = f.rows[idx];
            const bool focused = (idx == f.cursor) && !hdr_focus;
            const int y = kCaptionY + kCaptionRowH + i * kRowH;

            if (row.seg_count == 0 && row.summary_fn) {
                // Summary-only row (ADSR): full-width line, inverted when focused.
                snprintf(status, sizeof(status), "%s:%s", row.label, row.summary_fn());
                if (focused) {
                    display_->fill_rect(0, y - 1, DisplaySh1106::kWidth, kRowH, true);
                    display_->draw_text_px(status, 0, y, false);
                } else {
                    display_->draw_text(status, 0, y);
                }
                continue;
            }

            char cell[8];
            for (int si = 0; si < row.seg_count; ++si) {
                const Segment& s = row.segments[si];
                if (s.type == SegmentType::Param) {
                    snprintf(cell, sizeof(cell), "PRM");
                } else {
                    seg_value_text(s, cell, sizeof(cell));
                }
                const bool inverted = focused && (si == f.seg);
                const bool edited = inverted && engine.quick_edited();
                draw_quick_cell(*display_, si, y, cell, inverted, edited);
                // The span band is drawn only while THIS range cell is being
                // edited: the Rand row carries two range cells (pitch + length)
                // and two always-on bands would overlap and mislead.
                if (s.type == SegmentType::Range && focused && si == f.seg &&
                    engine.edit_mode()) {
                    draw_seg_range_band(*display_, s, y, true);
                }
            }

            // Row label on the left, shortened to fit the label gutter.
            char label[kLabelW / DisplaySh1106::kTextAdvance + 1];
            const int label_max = kLabelW / DisplaySh1106::kTextAdvance;
            const int len = static_cast<int>(strlen(row.label));
            const int n = len > label_max ? label_max : len;
            std::memcpy(label, row.label, static_cast<std::size_t>(n));
            label[n] = '\0';
            if (focused && f.seg == -1) {
                // Row-caption focus (the leftmost column): invert the label.
                display_->fill_rect(0, y - 1, kLabelW, kRowH, true);
                display_->draw_text_px(label, 0, y, false);
            } else {
                display_->draw_text(label, 0, y);
            }
        }
        return;
    }

    // Item list (DETAIL / MAIN)
    char line[kMaxCols + 1];
    int line_i = 0;
    if (engine.depth() > 1) {
        snprintf(status, sizeof(status), "<%s", engine.parent_label());
        display_->draw_text(status, 0, kContentY);
        line_i = 1;
    }
    for (; line_i < 6; ++line_i) {
        const int idx = f.offset + (line_i - (engine.depth() > 1 ? 1 : 0));
        if (idx < 0 || idx >= f.item_count) {
            break;
        }
        const MenuItem& it = f.items[idx];
        const bool focused = (idx == f.cursor);
        const bool editable = it.get_i != nullptr;
        const bool changed = focused && editable && it.get_i() != engine.item_snapshot();

        char value[kMaxCols];
        item_value_text(it, value, sizeof(value));

        int pos = 0;
        line[pos++] = focused ? '>' : ' ';
        const int label_len = static_cast<int>(strlen(it.label ? it.label : ""));
        const int label_max = label_len > 10 ? 10 : label_len;
        for (int j = 0; j < label_max && pos < kMaxCols; ++j) {
            line[pos++] = it.label[j];
        }
        if (it.type == MenuItemType::Section || it.type == MenuItemType::Group) {
            line[pos++] = ':';
            if (pos < kMaxCols) {
                line[pos++] = '>';
            }
        } else {
            if (pos < kMaxCols) {
                line[pos++] = ':';
            }
            if (it.type == MenuItemType::NoteRange && engine.editing_range() && focused) {
                // Range editor active: bracket the span.
                line[pos++] = '[';
                for (const char* p = value; *p && pos < kMaxCols; ++p) {
                    line[pos++] = *p;
                }
                if (pos < kMaxCols) {
                    line[pos++] = ']';
                }
            } else {
                for (const char* p = value; *p && pos < kMaxCols; ++p) {
                    line[pos++] = *p;
                }
            }
            if (changed && pos < kMaxCols) {
                line[pos++] = '+';
                line[pos++] = '-';
            }
        }
        line[pos] = '\0';
        const int row_y = kContentY + line_i * kRowH;
        display_->draw_text(line, 0, row_y);
        if (it.type == MenuItemType::NoteRange) {
            draw_range_band(*display_, it, row_y, focused);
        }
    }
}

}  // namespace drom
