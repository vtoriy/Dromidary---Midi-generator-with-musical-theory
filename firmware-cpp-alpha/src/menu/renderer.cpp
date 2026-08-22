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
constexpr int kMaxCols = 21;  // 132px / 6px per glyph

// QUICK 3-column cell grid: row label on the left, then three fixed columns so
// the cells of Key/Chord/Arp line up vertically and never shift when a value
// or the edit marker changes.
constexpr int kLabelW = 24;
constexpr int kColX[3] = {26, 62, 98};
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
// always clear which parameter each cell edits (fits inside 132px width).
constexpr int kCaptionY = 8;
constexpr int kCaptionRowH = 8;

// DETAIL/MAIN NoteRange: a thin band under the mini-range tracks the active
// min..max over the full 132px strip (left = note 12 / C0, right = 119 / B8).
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
        // "C1-C4": span string from the live min/max accessors.
        const int32_t lo = s.get_min ? s.get_min() : 0;
        const int32_t hi = s.get_max ? s.get_max() : 0;
        snprintf(out, cap, "%s%d-%s%d",
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
    const int32_t span = (kNoteRangeMax - kNoteRangeMin);
    int x = (span <= 0) ? 0
                        : (static_cast<int>(lo) - kNoteRangeMin) * DisplaySh1106::kWidth / span;
    int w = (span <= 0) ? DisplaySh1106::kWidth
                        : (static_cast<int>(hi) - kNoteRangeMin) * DisplaySh1106::kWidth / span - x;
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
    const int32_t span = (kNoteRangeMax - kNoteRangeMin);
    int x = (span <= 0) ? 0
                        : (static_cast<int>(lo) - kNoteRangeMin) * DisplaySh1106::kWidth / span;
    int w = (span <= 0) ? DisplaySh1106::kWidth
                        : (static_cast<int>(hi) - kNoteRangeMin) * DisplaySh1106::kWidth / span - x;
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
        header = (state.runtime.mode == PlayMode::RandomNote) ? "RND" : "KB";
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

    // Playback progress line in the thin row under the status bar.
    if (state.runtime.playing && state.active_pattern().length > 0) {
        const int w = DisplaySh1106::kWidth *
                      static_cast<int>(state.runtime.current_step) /
                      static_cast<int>(state.active_pattern().length);
        if (w > 0) {
            display_->fill_rect(0, kProgressY, w, 1, true);
        }
    }

    if (state.runtime.test_mode) {
        draw_test_screen(state, *display_);
        return;
    }

    if (state.runtime.screen_mode == ScreenMode::Animation) {
        animation_.render(*display_);
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
                if (s.type == SegmentType::Range) {
                    draw_seg_range_band(*display_, s, y, focused);
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