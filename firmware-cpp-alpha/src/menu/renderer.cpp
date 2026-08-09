#include "renderer.hpp"

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

// Status-bar "infographics": play triangle + rec dot + step progress line.
// The mode name is drawn at x=0; the transport icons live in the gutter right
// after it (x=36/43), so the octave/bpm/step meta must start AFTER the icons.
constexpr int kPlayIconX = 36;
constexpr int kRecIconX = 43;
constexpr int kStatusMetaX = 50;
constexpr int kIconY = 2;
constexpr int kIconSize = 3;
constexpr int kProgressY = 7;  // thin blank row between status and content

void draw_play_icon(DisplaySh1106& d) {
    // Right-pointing triangle.
    d.fill_rect(kPlayIconX + 0, kIconY + 0, 1, kIconSize, true);
    d.fill_rect(kPlayIconX + 1, kIconY + 1, 1, 1, true);
}

void draw_rec_icon(DisplaySh1106& d) {
    // Filled dot.
    d.fill_rect(kRecIconX, kIconY, kIconSize, kIconSize, true);
}

const char* screen_prefix(ScreenMode m) {
    switch (m) {
        case ScreenMode::Quick: return "Quick";
        case ScreenMode::Full: return "Menu";
        case ScreenMode::Animation: return "Anim";
    }
    return "?";
}

void seg_value_text(const Segment& s, char* out, int cap) {
    const int v = static_cast<int>(s.get());
    if (s.labels != nullptr && v >= 0 && v < s.count) {
        snprintf(out, cap, "%s", s.labels[v]);
    } else {
        snprintf(out, cap, "%d", v);
    }
}

const char* midi_note_glyph(uint8_t midi) {
    static const char* kGlyphNames[12] = {"C", "C#", "D", "D#", "E", "F",
                                          "F#", "G", "G#", "A", "A#", "B"};
    return kGlyphNames[midi % 12];
}

int midi_note_octave(uint8_t midi) {
    return static_cast<int>(midi / 12) - 1;
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
        d.fill_rect(x, y - 1, kCellW, kRowH, true);
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

    // Left segment: mode name (ends ~30 px, before the play/rec icons at x=36).
    display_->draw_text(screen_prefix(state.runtime.screen_mode), 0, kStatusY);

    // Middle segment: octave/bpm/step, starts right of the transport icons.
    char meta[kMaxCols + 1];
    if (state.runtime.mode == PlayMode::RandomNote) {
        // RandomNote is not a pattern; drop the step readout to leave room for
        // the two-note readout on the right (pressed key > generated note).
        snprintf(meta, sizeof(meta), "o%d %d",
                 static_cast<int>(state.runtime.base_octave),
                 static_cast<int>(state.active_pattern().timing.bpm));
    } else {
        snprintf(meta, sizeof(meta), "o%d %d s%d",
                 static_cast<int>(state.runtime.base_octave),
                 static_cast<int>(state.active_pattern().timing.bpm),
                 static_cast<int>(state.runtime.current_step));
    }
    display_->draw_text(meta, kStatusMetaX, kStatusY);

    char status[kMaxCols + 1];

    // Icon row: play triangle / rec dot (cheap fill_rect, no font glyphs).
    if (state.runtime.playing) {
        draw_play_icon(*display_);
    }
    if (state.runtime.recording) {
        draw_rec_icon(*display_);
    }
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
        for (int i = 0; i < 6; ++i) {
            const int idx = f.offset + i;
            if (idx < 0 || idx >= f.row_count) {
                break;
            }
            const QuickRow& row = f.rows[idx];
            const bool focused = (idx == f.cursor);
            const int y = kContentY + i * kRowH;

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
            }

            // Live-pressed note: shown right of the MODE cell in the Quick list
            // (the Mode row). It disappears as soon as the key is released.
            if (row.seg_count > 0 && std::strcmp(row.label, "Mode") == 0) {
                char note_txt[12] {};
                format_note_txt(state, note_txt, sizeof(note_txt));
                if (note_txt[0] != '\0') {
                    const int len = static_cast<int>(std::strlen(note_txt));
                    const int nx = kColX[0] + kCellW + kCellPad;
                    if (nx + len * DisplaySh1106::kTextAdvance <= DisplaySh1106::kWidth) {
                        display_->draw_text(note_txt, nx, y);
                    } else {
                        display_->draw_text(note_txt,
                                            DisplaySh1106::kWidth - len * DisplaySh1106::kTextAdvance,
                                            y);
                    }
                }
            }

            // Row label on the left, shortened to fit the label gutter.
            char label[kLabelW / DisplaySh1106::kTextAdvance + 1];
            const int label_max = kLabelW / DisplaySh1106::kTextAdvance;
            const int len = static_cast<int>(strlen(row.label));
            const int n = len > label_max ? label_max : len;
            std::memcpy(label, row.label, static_cast<std::size_t>(n));
            label[n] = '\0';
            display_->draw_text(label, 0, y);
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
            for (const char* p = value; *p && pos < kMaxCols; ++p) {
                line[pos++] = *p;
            }
            if (changed && pos < kMaxCols) {
                line[pos++] = '+';
                line[pos++] = '-';
            }
        }
        line[pos] = '\0';
        display_->draw_text(line, 0, kContentY + line_i * kRowH);
    }
}

}  // namespace drom
