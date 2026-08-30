#include "app_loop.hpp"

#include <algorithm>
#include <cstring>

#include "pico/stdlib.h"
#include "tusb.h"

#include "engine/midi_chain.hpp"
#include "persist.hpp"
#include "platform/board_pins.hpp"

namespace drom {

namespace {

constexpr uint8_t kMidiOctaveOffset = 2;
constexpr uint8_t kKeyDebounce = 5;
constexpr uint32_t kJoyRepeatMs = 190;
constexpr uint32_t kEditorTiltDeadtimeMs = 200;  // ignore tilts right after an editor click / entry
constexpr uint32_t kFlushMs = 40;
constexpr uint32_t kAnimFrameMs = 1000 / 12;  // 12 FPS
constexpr uint32_t kManualAnimWakeGuardMs = 400; // ignore wake edges after manual launch
constexpr int32_t kPatternLens[5] = {4, 8, 16, 32, 64}; // STEP/PLEN doubling scale
constexpr uint32_t kPersistSaveDelayMs = 500; // wait 0.5s of no edits before flashing

// MIDI Clock: 24 pulses per quarter note (as with a DIN sync box).
constexpr int kClockPpqn = 24;

// Functional buttons are on chip3, whose 8 bits are the HIGH byte of the
// 24-bit shift read (raw bits 16..23). These constants are RAW bit indices —
// the same convention the note keys use (raw bits 0..15) — so the whole input
// layer shares one pinout numbering. Actual wiring (confirmed on the 74HC165):
//   chip3 bit0(Play) bit1(Rest) bit2(Rec) bit3(Shift) bit4(unused) bit5(unused) bit6(OctUp "+") bit7(OctDown "-")
constexpr uint8_t kBtnPlay = 16;     // chip3 bit0
constexpr uint8_t kBtnRest = 17;     // chip3 bit1
constexpr uint8_t kBtnRec = 18;      // chip3 bit2
constexpr uint8_t kBtnShift = 19;    // chip3 bit3
constexpr uint8_t kBtnOctUp = 22;    // chip3 bit6 ("+")
constexpr uint8_t kBtnOctDown = 23;  // chip3 bit7 ("-")

// Shift + note-key hotkeys in the EDIT screen. The 16 raw note bits (0..15)
// are mapped positionally; bit 0 is the lowest white key etc. Copy/Paste/Dup
// sit together (C, C#, D) and Undo/Redo together (D#, E) for a logical layout.
constexpr uint8_t kKeyCopy = 0;         // C   -> copy visible page
constexpr uint8_t kKeyPaste = 1;        // C#  -> paste clipboard onto page
constexpr uint8_t kKeyDup = 2;          // D   -> duplicate (copy+paste) page
constexpr uint8_t kKeyUndo = 3;         // D#  -> undo last batch
constexpr uint8_t kKeyRedo = 4;         // E   -> redo

// Sentinel step index marking a length-only undo/redo entry (no step touched).
constexpr uint8_t kLenOnlyIndex = 0xFF;

constexpr uint32_t kHintMs = 700;  // how long a hotkey confirmation stays up

}  // namespace

void AppLoop::init() {
    stdio_init_all();
    init_default_state(state_);
    persist_load_click(state_.runtime.click);
    saved_click_ = state_.runtime.click;
    shift_.init();
    midi_.init();
    display_.init();
    joy_.init();
    mode_.init(&midi_, &state_);
    menu_.init(&state_);

    gpio_init(BoardPins::kLed);
    gpio_set_dir(BoardPins::kLed, GPIO_OUT);
    gpio_put(BoardPins::kLed, false);
    last_input_ms_ = to_ms_since_boot(get_absolute_time());
}

bool AppLoop::is_pressed(uint32_t raw, uint8_t bit) const {
    return ((raw >> bit) & 1u) == 0u;
}

uint8_t AppLoop::button_to_note(uint8_t index) const {
    // Buttons 1-12 = C..B of the current octave; buttons 13-16 continue the
    // keyboard one octave up (C, C#, D, D#), per the documented layout.
    uint8_t octave = state_.runtime.base_octave + kMidiOctaveOffset;
    if (index >= 12) {
        ++octave;
    }
    return static_cast<uint8_t>(octave * 12 + (index % 12));
}

void AppLoop::update_func(bool raw_bits[8], uint32_t now_ms) {
    // Independently debounce each functional button by wall-clock time: the
    // confirmed state only flips after the raw input has held the new value for
    // the configured debounce window. A bounce that re-reads the old value
    // resets the pending timer, so one physical click yields exactly one edge
    // regardless of how the switch rings. raw_bits[i] is the already-inverted
    // raw read for chip3 bit i (bit i = raw bit 16+i).
    const uint32_t debounce_ms = state_.runtime.click.debounce_ms;
    uint32_t bits = func_stable_;
    for (int i = 0; i < 8; ++i) {
        const bool pressed = raw_bits[i];
        const uint32_t bit = 16u + static_cast<uint32_t>(i);
        const bool cur = (func_stable_ >> bit) & 1u;
        if (pressed != cur) {
            if (func_pending_start_[i] == 0u) {
                func_pending_start_[i] = now_ms;
            } else if ((now_ms - func_pending_start_[i]) >= debounce_ms) {
                func_pending_start_[i] = 0;
                if (pressed) {
                    bits |= 1u << bit;
                } else {
                    bits &= ~(1u << bit);
                }
            }
        } else {
            func_pending_start_[i] = 0;
        }
    }
    func_prev_ = func_stable_;
    func_stable_ = bits;
}

bool AppLoop::fn_pressed(uint8_t bit) const {
    return (func_stable_ >> bit) & 1u;
}

bool AppLoop::fn_edge(uint8_t bit) const {
    return fn_pressed(bit) && !((func_prev_ >> bit) & 1u);
}

bool AppLoop::fn_fell(uint8_t bit) const {
    return !fn_pressed(bit) && ((func_prev_ >> bit) & 1u);
}

void AppLoop::process_functional(uint32_t raw, uint32_t now_ms) {
    auto& runtime = state_.runtime;

    // Feed the debouncer with the raw functional-button bits (chip3).
    bool raw_bits[8];
    for (int i = 0; i < 8; ++i) {
        raw_bits[i] = is_pressed(raw, 16u + static_cast<uint8_t>(i));
    }
    update_func(raw_bits, now_ms);

    // Functional buttons are also exposed as MIDI CC so a DAW can assign them
    // as control buttons (press = 127, release = 0). Guarded by test mode:
    // there the flat channel must stay silent so the tester sees only the
    // raw signal changes. Otherwise, the sender calls on fn_edge/fn_fell.
    if (!runtime.test_mode) {
        // [Play/Rest/Rec/Shift]/[Oct+/Oct-] -> a narrow set of CC numbers.
        constexpr uint8_t kCcChan = 15;  // 0-based MIDI channel 16 (away from notes)
        constexpr uint8_t kPlayCc = 20;
        constexpr uint8_t kRestCc = 21;
        constexpr uint8_t kRecCc = 22;
        constexpr uint8_t kShiftCc = 23;
        constexpr uint8_t kOctUpCc = 24;
        constexpr uint8_t kOctDnCc = 25;
        if (fn_edge(kBtnPlay)) { midi_.cc(kCcChan, kPlayCc, 127); }
        if (fn_fell(kBtnPlay)) { midi_.cc(kCcChan, kPlayCc, 0); }
        if (fn_edge(kBtnRest)) { midi_.cc(kCcChan, kRestCc, 127); }
        if (fn_fell(kBtnRest)) { midi_.cc(kCcChan, kRestCc, 0); }
        if (fn_edge(kBtnRec)) { midi_.cc(kCcChan, kRecCc, 127); }
        if (fn_fell(kBtnRec)) { midi_.cc(kCcChan, kRecCc, 0); }
        if (fn_edge(kBtnShift)) { midi_.cc(kCcChan, kShiftCc, 127); }
        if (fn_fell(kBtnShift)) { midi_.cc(kCcChan, kShiftCc, 0); }
        if (fn_edge(kBtnOctUp)) { midi_.cc(kCcChan, kOctUpCc, 127); }
        if (fn_fell(kBtnOctUp)) { midi_.cc(kCcChan, kOctUpCc, 0); }
        if (fn_edge(kBtnOctDown)) { midi_.cc(kCcChan, kOctDnCc, 127); }
        if (fn_fell(kBtnOctDown)) { midi_.cc(kCcChan, kOctDnCc, 0); }
    }

    // Diagnostic: mirror the CONFIRMED (debounced) chip3 image to the status
    // strip. It only changes when a key really settles, so bounces and
    // crosstalk never flicker the readout.
    const uint8_t diag = static_cast<uint8_t>((func_stable_ >> 16u) & 0xFFu);
    if (diag != runtime.func_bits) {
        runtime.func_bits = diag;
        ui_dirty_ = true;
    }

    // In test mode the buttons are watched, not acted upon: playing a key must
    // not start/stop transport or move the octave while the tester inspects the
    // raw signals.
    if (runtime.test_mode) {
        return;
    }

    const bool shift_held = fn_pressed(kBtnShift);
    const bool rest_held = fn_pressed(kBtnRest);

    // Octave up/down: a single confirmed press moves one step. The min/max
    // bounds (0..8 plus the +2 MIDI offset) keep the keyboard inside MIDI.
    if (fn_edge(kBtnOctUp) && runtime.base_octave < 8) {
        ++runtime.base_octave;
        ui_dirty_ = true;
    }
    if (fn_edge(kBtnOctDown) && runtime.base_octave > 0) {
        --runtime.base_octave;
        ui_dirty_ = true;
    }

    // Play: debounced toggle. Single press -> single state flip regardless of
    // contact bounce; also starts/stops the continuous RandomNote generation.
    // In RandomNote a loop can be started by a first key press, so Play acts as
    // stop-if-running / start-if-stopped rather than a pure `playing` toggle,
    // otherwise the first Play press would re-anchor instead of stopping.
    if (fn_edge(kBtnPlay)) {
        runtime.playing = !runtime.playing;
        if (state_.runtime.screen_mode == ScreenMode::Edit) {
            // In the editor Play ALWAYS auditions the pattern slot (no
            // generation) — whatever play mode the user came from. This makes
            // toggling predictable and stops the mismatch where the icon
            // shows "playing" but nothing advances.
            mode_.pattern_toggle(now_ms);
            runtime.playing = mode_.pattern_running();
        } else if (runtime.mode == PlayMode::RandomNote) {
            if (mode_.random_loop_running()) {
                runtime.playing = false;
                mode_.random_loop_stop();
            } else {
                runtime.playing = true;
                // Anchor on the root of the current tonality, in the keyboard
                // octave (REP maps it into the PITCH range by pitch class).
                const uint8_t anchor = static_cast<uint8_t>(
                    (runtime.base_octave + kMidiOctaveOffset) * 12 +
                    state_.active_pattern().key_filter.root_note);
                mode_.random_loop_start(anchor, now_ms);
            }
        } else if (runtime.mode == PlayMode::RandomPattern) {
            // GEN: Play starts/stops the loop without regenerating; a key
            // press is what creates a new pattern.
            mode_.gen_toggle_play();
            runtime.playing = mode_.gen_running();
        } else if (runtime.mode == PlayMode::Pattern) {
            // PTRN slot playback (audition while editing / after recording).
            mode_.pattern_toggle(now_ms);
            runtime.playing = mode_.pattern_running();
        } else if (runtime.mode == PlayMode::MidiKeyboard || runtime.mode == PlayMode::MidiFilter) {
            // Play as a transport stop for the live arpeggio: one press silences
            // everywhere the arp/keyboard is sounding, keeping a binary `playing`
            // state that the status bar reflects. Pressing Play again while idle
            // just flips the flag; the next key press restarts the arp.
            if (mode_.any_active_input()) {
                runtime.playing = false;
                mode_.all_notes_off();
            }
        }
        ui_dirty_ = true;
    }

    if (fn_edge(kBtnRec)) {
        runtime.recording = !runtime.recording;
        // Recording arms the silent pattern grid so that whatever the player
        // hears is quantised into the active slot: KB/Filter keys, RandomNote
        // output (and PTRN live playback all record while Rec is held up).
        if (!runtime.test_mode) {
            if (runtime.screen_mode == ScreenMode::Edit) {
                // In the editor Rec ONLY arms step entry — it must not start
                // the slot transport, otherwise the playhead runs away instead
                // of letting the user place notes one step at a time.
                mode_.capture_transport_stop();
            } else if (runtime.recording &&
                       (runtime.mode == PlayMode::Pattern ||
                        runtime.mode == PlayMode::RandomNote ||
                        runtime.mode == PlayMode::MidiKeyboard ||
                        runtime.mode == PlayMode::MidiFilter)) {
                mode_.capture_transport_start(now_ms);
            } else {
                mode_.capture_transport_stop();
            }
        }
        ui_dirty_ = true;
    }

    if (fn_edge(kBtnRest)) {
        if (state_.runtime.screen_mode == ScreenMode::Edit) {
            auto& ed = state_.editor;
            if (fn_pressed(kBtnShift)) {
                // Shift+Rest clears the whole visible 16-step page.
                editor_erase_page();
            } else if (state_.runtime.recording && mode_.pattern_running()) {
                // Rec+Play live: a held Rest writes a pause. Remember where and
                // when it was pressed; the length is applied on release.
                rest_cap_ms_ = now_ms;
                rest_cap_step_ = state_.runtime.current_step;
            } else if (ed.field == 0) {
                // Rest in NOTE focus restores the ORIGINAL note of the current
                // step (undo the pitch change) instead of erasing — when the
                // step has an origin recorded.
                const uint8_t idx =
                    std::min<uint8_t>(ed.selected, kStepCountMax - 1);
                const int16_t orig = ed.prev_notes[idx];
                if (orig >= 0) {
                    Step& s = state_.active_pattern().steps[idx];
                    s.notes[0] = static_cast<uint8_t>(orig);
                    if (orig == 0) {
                        // The step was empty originally -> back to a rest.
                        s.note_count = 0;
                        s.active = false;
                    }
                    ed.prev_notes[idx] = -1;  // undo consumed
                    ed.prev_note = -1;
                } else {
                    editor_erase_step();
                }
            } else {
                // Rest without a NOTE undo context erases the focused step.
                editor_erase_step();
            }
            // Rec WITHOUT Play records rest-by-rest too: after erasing (or
            // reverting) the focused step the cursor advances, matching how a
            // note key steps forward. Shift+Rest (whole page) is excluded.
            if (!fn_pressed(kBtnShift) && state_.runtime.recording &&
                !mode_.pattern_running()) {
                editor_move(1);
            }
        }
    }

    // Rec+Play live editing: releasing a held Rest commits a pause as long as
    // the hold. Every spanned grid step is cleared to a non-tie empty, so
    // playback forces a cutoff across the whole range ("hold Rest = pause").
    if (fn_fell(kBtnRest)) {
        if (state_.runtime.screen_mode == ScreenMode::Edit &&
            state_.runtime.recording && mode_.pattern_running()) {
            Pattern& pat = state_.active_pattern();
            const int length = std::max<int>(
                1, std::min<int>(pat.length, kStepCountMax));
            float quarter = 60000.0f;
            float bpm_f = pat.timing.bpm;
            if (bpm_f < 20.0f) { bpm_f = 120.0f; }
            quarter /= bpm_f;
            const float step_f = pat.grid64 ? quarter / 16.0f : quarter / 4.0f;
            const uint32_t step_ms = static_cast<uint32_t>(step_f < 1.0f ? 1.0f : step_f);
            const uint32_t held = now_ms - rest_cap_ms_;
            uint32_t n = (step_ms > 0) ? held / step_ms : 0u;
            if (n < 1) { n = 1; }
            const uint8_t idx0 = rest_cap_step_;
            for (uint32_t k = 0; k < n; ++k) {
                const int ti = static_cast<int>((idx0 + k) % length);
                Step& s = pat.steps[ti];
                if (s.active || s.tie) {
                    s.active = false;
                    s.note_count = 0;
                    s.notes[0] = 0;
                    s.tie = false;
                    s.len_div = 8;
                }
            }
            ui_dirty_ = true;
        }
    }

    // Live-mute is a "hold Rest while playing" behaviour: the output is muted
    // for as long as the button is held, and released immediately after.
    runtime.live_mute = rest_held;

    const bool rest_click = fn_edge(kBtnRest);
    if (rest_click && shift_held) {
        menu_.reset_value();
        ui_dirty_ = true;
    }
}

void AppLoop::process_notes(uint32_t raw, uint32_t now_ms) {
    for (uint8_t i = 0; i < kKeyCount; ++i) {
        const bool pressed = is_pressed(raw, i);
        // Per-key debounce: the confirmed state only flips after the raw
        // input has stayed at the new value for kKeyDebounce frames. The old
        // code compared against previous_raw_ (updated every frame), so the
        // counter could only ever fire during switch bounce and a clean
        // release was missed, leaving notes stuck on.
        if (pressed == key_state_[i]) {
            note_debounce_[i] = 0;
            continue;
        }
        ++note_debounce_[i];
        if (note_debounce_[i] < kKeyDebounce) {
            continue;
        }
        note_debounce_[i] = 0;
        key_state_[i] = pressed;

        if (pressed) {
            if (!note_held_[i]) {
                note_held_[i] = true;
                if (!state_.runtime.test_mode) {
                    // In the EDIT screen a held Shift turns the 16 note keys
                    // into HOTKEYS (copy/paste/undo/redo/dup) instead of note
                    // entry. Without Shift a note key assigns a pitch as usual.
                    if (state_.runtime.screen_mode == ScreenMode::Edit &&
                        fn_pressed(kBtnShift)) {
                        editor_shortcut(i, now_ms);
                    } else if (state_.runtime.screen_mode == ScreenMode::Edit &&
                               state_.editor.sel_mode) {
                        // In the SELECT sub-mode a bare note key must not edit
                        // a step — it only shapes the marked range.
                    } else if (state_.runtime.screen_mode == ScreenMode::Edit) {
                        // Note-key step write is an undoable single-step batch.
                        const int idx = std::min<int>(
                            state_.runtime.current_step, kStepCountMax - 1);
                        Step old = state_.active_pattern().steps[idx];
                        const int16_t old_prev = state_.editor.prev_notes[idx];
                        mode_.note_on(i, button_to_note(i), now_ms);
                        const auto& ns = state_.active_pattern().steps[idx];
                        const bool changed =
                            !(old.active == ns.active &&
                              old.notes[0] == ns.notes[0] &&
                              old.note_count == ns.note_count &&
                              old.len_div == ns.len_div && old.tie == ns.tie);
                        if (changed) {
                            ed_undo_begin();
                            ed_undo_record(static_cast<uint8_t>(idx), old, old_prev);
                        }
                        // Rec WITHOUT Play records step-by-step: after placing a
                        // note the cursor advances to the next step, ready for
                        // the next one. (With Play the transport drives the
                        // cursor instead and duration comes from the hold.)
                        if (state_.runtime.recording &&
                            !mode_.pattern_running()) {
                            editor_move(1);
                        }
                    } else {
                        mode_.note_on(i, button_to_note(i), now_ms);
                        // A RandomNote key press starts the loop without the
                        // Play button, so reflect transport in the status dial.
                        if (state_.runtime.mode == PlayMode::RandomNote &&
                            mode_.random_loop_running()) {
                            state_.runtime.playing = true;
                        }
                    }
                }
                ui_dirty_ = true;
            }
        } else {
            if (note_held_[i]) {
                note_held_[i] = false;
                if (!state_.runtime.test_mode) {
                    mode_.note_off(i, now_ms);
                }
                ui_dirty_ = true;
            }
        }
    }
}

void AppLoop::process_joystick(uint32_t raw, uint32_t now_ms) {
    joy_.poll();

    // Debounce the joystick button the same way as the note/function keys: the
    // stable state only flips after the raw input holds for kKeyDebounce frames.
    const bool raw_btn = joy_.button();
    if (raw_btn == joy_btn_stable_) {
        joy_btn_debounce_ = 0;
    } else {
        ++joy_btn_debounce_;
        if (joy_btn_debounce_ >= kKeyDebounce) {
            joy_btn_debounce_ = 0;
            joy_btn_stable_ = raw_btn;
        }
    }
    const bool btn = joy_btn_stable_;

    // In test mode the joystick does two things only: Shift+one click exits the
    // test screen back to the FULL menu. Everything else (navigation, radial
    // zones, long-press screen switch) is suppressed.
    if (state_.runtime.test_mode) {
        if (btn && !joy_btn_prev_) {
            joy_btn_press_ms_ = now_ms;
            joy_btn_long_ = false;
        }
        if (!btn && joy_btn_prev_ && !joy_btn_long_ && fn_pressed(kBtnShift)) {
            state_.runtime.test_mode = false;
            mode_.all_notes_off();
            menu_.rebuild();
            ui_dirty_ = true;
        }
        joy_btn_prev_ = btn;
        return;
    }

    if (btn && !joy_btn_prev_) {
        joy_btn_press_ms_ = now_ms;
        joy_btn_long_ = false;
    }
    if (btn && !joy_btn_long_ && (now_ms - joy_btn_press_ms_) >= state_.runtime.click.long_ms) {
        joy_btn_long_ = true;
        menu_.press_long();
        // If the long-press just dropped us into the pattern editor, ignore an
        // immediate tilt so the deflection that produced the hold cannot start
        // editing a note before the hand settles.
        if (state_.runtime.screen_mode == ScreenMode::Edit) {
            joy_suppress_tilt_until_ = now_ms + kEditorTiltDeadtimeMs;
            last_joy_dir_ = Direction::Center;
            joy_hold_ms_ = now_ms;
            last_joy_tilt_ms_ = 0;
        }
        ui_dirty_ = true;
        mode_.on_arp_config_changed(now_ms);
    }
    if (!btn && joy_btn_prev_) {
        if (!joy_btn_long_) {
            if (state_.runtime.screen_mode == ScreenMode::Edit) {
                // Editor click: Shift+click toggles the SELECT sub-mode;
                // Rest+click erases the focused step; a plain click SELECTS the
                // step under the cursor (so note keys assign to it) and cycles
                // the focused field.
                if (fn_pressed(kBtnShift)) {
                    editor_select_toggle();
                } else if (fn_pressed(kBtnRest)) {
                    editor_erase_step();
                } else {
                    editor_cycle_field();  // also selects the cursor step
                }
                // The joystick is often still slightly deflected right after the
                // click that selected/edited; swallow an immediate tilt so it
                // cannot move the cursor or edit a note the moment we enter a
                // field.
                joy_suppress_tilt_until_ = now_ms + kEditorTiltDeadtimeMs;
            } else {
                // Rest + click resets the current value (QUICK cell or
                // DETAIL/MAIN item). Otherwise a double-click in DETAIL/MAIN is
                // the alternative reset, and a plain click is a normal
                // confirm/edit action.
                if (fn_pressed(kBtnRest)) {
                    menu_.reset_value();
                } else if (menu_.editing_value_item() &&
                           (now_ms - joy_btn_last_click_ms_) < state_.runtime.click.double_ms) {
                    menu_.reset_value();
                } else {
                    menu_.press_short();
                }
            }
            joy_btn_last_click_ms_ = now_ms;
            ui_dirty_ = true;
            mode_.on_arp_config_changed(now_ms);
        }
        joy_btn_long_ = false;
    }
    joy_btn_prev_ = btn;

    const Direction dir = joy_.direction();
    if (dir != Direction::Center) {
        if (dir != last_joy_dir_) {
            last_joy_dir_ = dir;
            joy_hold_ms_ = now_ms;
            last_joy_tilt_ms_ = 0;
        }
        // Accelerating auto-repeat: while the joystick stays deflected in the
        // same direction, the tilt interval shrinks (and, at the fastest tier,
        // several tilts fire per tick) so long holds sweep wide ranges quickly
        // without the initial repeat being too jumpy.
        uint32_t interval = kJoyRepeatMs;
        uint32_t taps = 1;
        const uint32_t hold = now_ms - joy_hold_ms_;
        if (hold > 2500) {
            interval = 45;
            taps = 4;
        } else if (hold > 1400) {
            interval = 70;
            taps = 3;
        } else if (hold > 700) {
            interval = 100;
            taps = 2;
        } else if (hold > 350) {
            interval = 140;
            taps = 1;
        }
        if (now_ms < joy_suppress_tilt_until_) {
            // Right after a click or entering the editor the joystick often
            // still reads a small deflection from the same motion; swallow it so
            // it cannot immediately move the cursor or edit a note. The hold is
            // reset so a later deliberate tilt starts from a fresh repeat.
            last_joy_dir_ = Direction::Center;
            joy_hold_ms_ = now_ms;
            last_joy_tilt_ms_ = 0;
        } else if ((now_ms - last_joy_tilt_ms_) >= interval) {
            last_joy_tilt_ms_ = now_ms;
            const bool shift = fn_pressed(kBtnShift);
            if (state_.runtime.screen_mode == ScreenMode::Edit) {
                for (uint32_t k = 0; k < taps; ++k) {
                    editor_tilt(dir, shift);
                }
            } else {
                for (uint32_t k = 0; k < taps; ++k) {
                    if (menu_.editing_radial()) {
                        menu_.radial_select(direction_to_zone(dir));
                    } else {
                        menu_.tilt(dir, shift);
                    }
                }
            }
            ui_dirty_ = true;
            mode_.on_arp_config_changed(now_ms);
        }
    } else {
        last_joy_dir_ = Direction::Center;
    }
}

void AppLoop::update_midi_clock(uint32_t now_ms) {
    const TimingCfg& t = state_.active_pattern().timing;
    const ClockSync mode = t.clock;

    // Master: emit 24ppqn Start/Stop + ticks whenever the pattern is running.
    if (mode == ClockSync::Master) {
        if (state_.runtime.playing) {
            if (!master_started_) {
                midi_.realtime(0xFA);  // Start
                master_started_ = true;
                clock_last_now_ms_ = now_ms;
                clock_acc_64th_ = 0;
            }
            const int bpm = static_cast<int>(t.bpm);
            if (bpm > 0) {
                // Drift-free tick clock: accumulate REAL elapsed time (in 1/64
                // ms units) and emit an F8 every step = 160000/bpm units
                // (= 60000/bpm/24 ms). Scheduling latency of the main loop no
                // longer shifts the tempo, because every frame contributes its
                // exact delta and the accumulator carries the remainder.
                const uint32_t elapsed = now_ms - clock_last_now_ms_;
                clock_last_now_ms_ = now_ms;
                clock_acc_64th_ += elapsed << 6;
                uint32_t step = 160000u / static_cast<uint32_t>(bpm);
                if (step == 0) { step = 1; }
                // Runaway guard: after a stall emit at most a few catch-up
                // ticks instead of a burst, then resync.
                if (clock_acc_64th_ > step * 8u) {
                    clock_acc_64th_ = 0;
                }
                while (clock_acc_64th_ >= step) {
                    clock_acc_64th_ -= step;
                    midi_.realtime(0xF8);
                }
            }
        } else {
            if (master_started_) {
                midi_.realtime(0xFC);  // Stop
                master_started_ = false;
            }
            clock_acc_64th_ = 0;
        }
        return;
    }

    // Slave: derive the tempo from the incoming F8 stream and mirror the host
    // transport (Start/Continue -> running, Stop -> stopped).
    if (mode == ClockSync::Slave) {
        uint8_t rt = midi_.poll_realtime();
        bool saw_tick = false;
        while (rt != 0) {
            switch (rt) {
                case 0xFA:  // Start
                case 0xFB:  // Continue
                    slave_running_ = true;
                    state_.runtime.playing = true;
                    ui_dirty_ = true;
                    break;
                case 0xFC:  // Stop
                    slave_running_ = false;
                    state_.runtime.playing = false;
                    mode_.all_notes_off();
                    ui_dirty_ = true;
                    break;
                case 0xF8:  // Clock tick
                    saw_tick = true;
                    break;
                default:
                    break;
            }
            rt = midi_.poll_realtime();
        }
        if (saw_tick) {
            if (clock_last_rx_ms_ != 0 && now_ms > clock_last_rx_ms_) {
                clock_sample_interval_ = now_ms - clock_last_rx_ms_;
                if (clock_avg_interval_ == 0) {
                    clock_avg_interval_ = clock_sample_interval_;
                } else {
                    clock_avg_interval_ = (clock_avg_interval_ + clock_sample_interval_) / 2;
                }
                // 24 ticks per quarter => bpm = 60000 / (interval * 24).
                const uint32_t interval = clock_avg_interval_ > 0 ? clock_avg_interval_ : 1;
                const int32_t avg_bpm = static_cast<int32_t>(60000 / (interval * kClockPpqn));
                if (avg_bpm >= 20 && avg_bpm <= 300) {
                    state_.active_pattern().timing.bpm = static_cast<uint16_t>(avg_bpm);
                    ui_dirty_ = true;
                }
            }
            clock_last_rx_ms_ = now_ms;
        }
        // A dead clock line with nothing arriving each loop resets nothing —
        // the host and device share one USB bus, so silence means the master
        // stopped sending; Stop usually arrives first in that case.
        return;
    }
}

// -- Pattern editor input (ScreenMode::Edit) ---------------------------------
void AppLoop::editor_move(int delta) {
    auto& ed = state_.editor;
    Pattern& p = state_.active_pattern();
    const int len = std::max<int>(1, std::min<int>(p.length, kStepCountMax));
    // cur is an ABSOLUTE step index; the page is always cur/16. Moving simply
    // increments/decrements the absolute index and wraps over the loop, so
    // page transitions (16->17, 17->16) happen automatically and stepping off
    // either end of the loop wraps (16-step loop: 16->1 when tilting right,
    // 1->16 when tilting left).
    int nc = static_cast<int>(ed.cur) + delta;
    if (nc < 0) {
        nc = len - 1;              // left of step 1 -> wrap to last step
    } else if (nc >= len) {
        nc = 0;                    // right of last step -> wrap to step 1
    }
    ed.cur = static_cast<uint8_t>(nc);
    const uint8_t new_page = static_cast<uint8_t>(static_cast<int>(ed.cur) / 16);
    // Normal cursor navigation that crosses a page boundary clears any marked
    // range: the selection is page-local unless it is being shaped in SELECT
    // mode (which never routes through here).
    if (ed.sel_active && new_page != ed.page) {
        ed.sel_active = false;
    }
    ed.page = new_page;
    state_.runtime.current_step = ed.cur;
}



void AppLoop::editor_tilt(Direction dir, bool shift) {
    auto& ed = state_.editor;
    Pattern& p = state_.active_pattern();
    Step& s = p.steps[std::min<int>(ed.cur, kStepCountMax - 1)];
    const int len = std::max<int>(1, std::min<int>(p.length, kStepCountMax));

    // SELECT sub-mode: the range is a bounded block on the current page, only
    // loosely tied to the cursor. Left/Right shift the START edge, Up/down
    // expand/shrink the length (the right edge). It never moves the cursor/
    // playhead, so recording position and the selection are independent.
    if (ed.sel_mode) {
        // The range is confined to the visible page (columns of the current
        // screen), so it stays fully in view and is decoupled from the cursor
        // step, not the page. Left/Right SHIFT the whole block (start and end
        // together, keeping its length); Up/Down expand (right edge out) or
        // shrink (right edge in) the length.
        const int page_lo = static_cast<int>(ed.page) * 16;
        const int page_hi = std::min(page_lo + 15, len - 1);
        int a = std::clamp(static_cast<int>(ed.sel_a), page_lo, page_hi);
        int b = std::clamp(static_cast<int>(ed.sel_b), page_lo, page_hi);
        switch (dir) {
            case Direction::Left:
                // move the whole block left by one, clamped to the page
                if (a > page_lo) {
                    --a;
                    --b;
                }
                break;
            case Direction::Right:
                // move the whole block right by one, clamped to the page
                if (b < page_hi) {
                    ++a;
                    ++b;
                }
                break;
            case Direction::Up:   // expand: push the right edge further out
                b = std::min(page_hi, b + 1);
                break;
            case Direction::Down: // shrink: pull the right edge back in
                b = std::max(a, b - 1);
                break;
            default:
                break;
        }
        ed.sel_a = static_cast<uint8_t>(a);
        ed.sel_b = static_cast<uint8_t>(b);
        ed.sel_active = true;
        ui_dirty_ = true;
        return;
    }

    if (dir == Direction::Left || dir == Direction::Right) {
        const int d = (dir == Direction::Right) ? 1 : -1;
        if (shift && ed.field != 3) {
            // Shift+tilt flips the visible 16-step page while keeping the
            // cursor column: jump a full page of steps (wrapped over the
            // loop, so page/page are recomputed consistently).
            editor_move(d * 16);
        } else if (ed.field == 3) {
            // Focused PLEN: pattern length in doubling steps. Shrinking only
            // narrows the loop — the trimmed steps' notes are KEPT, so
            // re-expanding restores them (nothing is destroyed).
            int idx = 2;
            for (int i = 0; i < 5; ++i) {
                if (kPatternLens[i] == len) { idx = i; break; }
            }
            idx = std::clamp(idx + d, 0, 4);
            const int new_len = static_cast<int>(kPatternLens[idx]);
            if (new_len != len) {
                ed_undo_len(static_cast<uint8_t>(new_len));
                // Clamp the cursor/selection back into the shortened loop.
                if (static_cast<int>(ed.cur) >= new_len) {
                    ed.cur = static_cast<uint8_t>(new_len - 1);
                }
                if (static_cast<int>(ed.selected) >= new_len) {
                    ed.selected = ed.cur;
                }
                ed.page = static_cast<uint8_t>(static_cast<int>(ed.cur) / 16);
                state_.runtime.current_step = ed.cur;
            }
        } else {
            editor_move(d);
        }
        return;
    }

    const int delta = (dir == Direction::Up) ? 1 : -1;
    switch (ed.field) {
        case 0: {  // NOTE: chromatic pitch, Shift = octave ±12
            const int base = s.active && s.note_count > 0 ? s.notes[0]
                                                          : static_cast<int>(60);
            const uint8_t cidx = std::min<uint8_t>(ed.cur, kStepCountMax - 1);
            Step old = s;
            const int16_t old_prev = ed.prev_notes[cidx];
            // Latch the step's ORIGINAL note the first time it is touched so
            // it can be restored (Rest) and shown as the +/- direction.
            if (ed.prev_notes[cidx] < 0) {
                ed.prev_notes[cidx] = static_cast<int16_t>(s.notes[0]);
            }
            ed.prev_note = ed.prev_notes[cidx];
            const int stp = shift ? 12 : 1;
            const int v = std::clamp<int>(base + delta * stp,
                                          kNoteRangeMin, kNoteRangeMax);
            s.notes[0] = static_cast<uint8_t>(v);
            s.note_count = 1;
            s.active = true;
            ui_dirty_ = true;
            ed_undo_begin();
            ed_undo_record(cidx, old, old_prev);
            break;
        }
        case 1: {  // LEN: cycle the visible divisions
            const bool tr = p.random.len_triplets;
            int pos = note_len_div_pos(s.len_div, tr);
            pos = std::clamp<int>(pos + delta, 0, note_len_div_count(tr) - 1);
            const uint8_t cidx = std::min<uint8_t>(ed.cur, kStepCountMax - 1);
            Step old = s;
            const int16_t old_prev = ed.prev_notes[cidx];
            s.len_div = note_len_div_real(pos, tr);
            ed_undo_begin();
            ed_undo_record(cidx, old, old_prev);
            break;
        }
        case 2: {  // ON: toggle rest/note
            const uint8_t cidx = std::min<uint8_t>(ed.cur, kStepCountMax - 1);
            Step old = s;
            const int16_t old_prev = ed.prev_notes[cidx];
            s.active = !s.active;
            s.note_count = s.active ? 1 : 0;
            ed_undo_begin();
            ed_undo_record(cidx, old, old_prev);
            break;
        }
        default:
            break;  // PLEN handled above via left/right
    }
}

void AppLoop::editor_cycle_field() {
    auto& ed = state_.editor;
    auto& f = ed.field;
    f = static_cast<uint8_t>((f + 1) % 4);
    // Selecting happens on the step under the cursor; note keys then assign
    // to this step while the pointer just navigates. Also snap the cursor to
    // the selected step's page so the selection stays in view. A click either
    // selects a NEW step or cycles off NOTE — in both cases it's a confirmed
    // transition, so the pitch-undo context starts fresh (prev_note reset).
    ed.selected = ed.cur;
    ed.has_selected = true;
    // Carry the selected step's ORIGINAL note forward so Rest can still undo
    // and the +/- direction stays correct when the user re-visits the step.
    ed.prev_note = ed.prev_notes[std::min<std::size_t>(ed.selected, kStepCountMax - 1)];
    Pattern& p = state_.active_pattern();
    const int len = std::max<int>(1, std::min<int>(p.length, kStepCountMax));
    ed.page = static_cast<uint8_t>(static_cast<int>(ed.selected) / 16);
    if (static_cast<int>(ed.page) * 16 >= len) {
        ed.page = static_cast<uint8_t>(std::max(0, (len + 15) / 16 - 1));
    }
}

void AppLoop::editor_erase_step() {
    auto& ed = state_.editor;
    const uint8_t idx = std::min<uint8_t>(ed.cur, kStepCountMax - 1);
    Step old = state_.active_pattern().steps[idx];
    const int16_t old_prev = ed.prev_notes[idx];
    Step& s = state_.active_pattern().steps[idx];
    s.active = false;
    s.note_count = 0;
    s.notes[0] = 0;
    s.tie = false;
    ed.prev_notes[idx] = -1;  // erased -> no original to restore
    // Revertible as one undoable unit (Rest on a step without a NOTE context).
    ed_undo_begin();
    ed_undo_record(idx, old, old_prev);
}

void AppLoop::editor_erase_page() {
    auto& ed = state_.editor;
    Pattern& p = state_.active_pattern();
    const int len = std::max<int>(1, std::min<int>(p.length, kStepCountMax));
    const int start = static_cast<int>(ed.page) * 16;
    const int end = std::min(start + 16, len);
    ed_undo_begin();
    for (int i = start; i < end; ++i) {
        Step old = p.steps[i];
        const int16_t old_prev = ed.prev_notes[i];
        auto& s = p.steps[i];
        s.active = false;
        s.note_count = 0;
        s.notes[0] = 0;
        s.tie = false;
        ed.prev_notes[i] = -1;
        ed_undo_record(static_cast<uint8_t>(i), old, old_prev);
    }
}

void AppLoop::editor_select_toggle() {
    auto& ed = state_.editor;
    if (!ed.sel_mode && !ed.sel_active) {
        // No selection yet: enter SELECT and anchor the range to the WHOLE
        // visible page, starting at the first step of the screen (not at the
        // recording cursor). The range stays highlighted after leaving SELECT.
        const int len = std::max<int>(
            1, std::min<int>(state_.active_pattern().length, kStepCountMax));
        const int start = static_cast<int>(ed.page) * 16;
        const int end = std::min(start + 16, len) - 1;
        ed.sel_a = static_cast<uint8_t>(std::max(0, start));
        ed.sel_b = static_cast<uint8_t>(std::max(0, end));
        ed.sel_active = true;
        ed.sel_mode = true;
    } else if (ed.sel_mode) {
        // Exit SELECT: the range remains active/visible for later operations.
        ed.sel_mode = false;
    } else {
        // Not shaping but a range is still marked: clear the selection.
        ed.sel_active = false;
        ed.sel_a = 0;
        ed.sel_b = 0;
    }
    ui_dirty_ = true;
}

void AppLoop::set_hint(uint8_t value, uint32_t now_ms) {
    auto& ed = state_.editor;
    ed.hint = value;
    ed.hint_until_ms = now_ms + kHintMs;
}

void AppLoop::ed_undo_begin() {
    auto& ed = state_.editor;
    // A brand-new edit invalidates the redo path.
    ed.redo_size = 0;
    ed.redo_marks_count = 0;
    ed.undo_len_before = state_.active_pattern().length;
    if (ed.undo_marks_count < kUndoMarksMax) {
        ed.undo_marks[ed.undo_marks_count++] = ed.undo_size;
    }
}

void AppLoop::ed_undo_record(uint8_t index, const Step& old_step,
                             int16_t old_prev) {
    auto& ed = state_.editor;
    if (ed.undo_size >= kUndoDepth) {
        // Log full: drop the oldest batch so a fresh command always fits.
        // The oldest batch is undo_buf[marks[0] .. next_boundary); every
        // remaining mark and entry shifts left by the dropped length.
        if (ed.undo_marks_count > 0) {
            const uint8_t end_first = (ed.undo_marks_count >= 2)
                                          ? ed.undo_marks[1]
                                          : ed.undo_size;
            const uint8_t drop = static_cast<uint8_t>(
                end_first - ed.undo_marks[0]);
            if (drop > 0) {
                std::move(&ed.undo_buf[end_first], &ed.undo_buf[ed.undo_size],
                          &ed.undo_buf[0]);
                ed.undo_size = static_cast<uint8_t>(ed.undo_size - drop);
            }
            for (uint8_t m = 1; m < ed.undo_marks_count; ++m) {
                const uint8_t v = ed.undo_marks[m];
                ed.undo_marks[m - 1] = static_cast<uint8_t>(
                    v >= end_first ? v - drop : 0);
            }
            ed.undo_marks_count = static_cast<uint8_t>(ed.undo_marks_count - 1);
            if (ed.undo_size >= kUndoDepth) {
                return;  // still no room; ignore this tiny record
            }
        } else {
            return;
        }
    }
    auto& e = ed.undo_buf[ed.undo_size++];
    e.index = index;
    e.old_step = old_step;
    e.new_step = state_.active_pattern().steps[index];
    e.old_prev = old_prev;
    e.new_prev = ed.prev_notes[index];
    e.len_before = ed.undo_len_before;
    e.len_after = state_.active_pattern().length;
}

void AppLoop::ed_undo_len(uint8_t new_len) {
    // A loop-length change that must NOT touch any step (shrinking keeps the
    // trimmed notes). Record it as a single length-only entry (sentinel index)
    // and set the length; undo/redo restore just the length, not steps.
    auto& ed = state_.editor;
    ed.redo_size = 0;
    ed.redo_marks_count = 0;
    ed.undo_len_before = state_.active_pattern().length;
    if (ed.undo_marks_count < kUndoMarksMax) {
        ed.undo_marks[ed.undo_marks_count++] = ed.undo_size;
    }
    if (ed.undo_size < kUndoDepth) {
        auto& e = ed.undo_buf[ed.undo_size++];
        e.index = kLenOnlyIndex;
        e.len_before = ed.undo_len_before;
        e.len_after = new_len;
    }
    state_.active_pattern().length = new_len;
}

bool AppLoop::ed_undo() {
    auto& ed = state_.editor;
    if (ed.undo_marks_count == 0 || ed.undo_size == 0) {
        return false;
    }
    const uint8_t m = ed.undo_marks[ed.undo_marks_count - 1];
    const uint8_t batch_len = static_cast<uint8_t>(ed.undo_size - m);
    const uint8_t redo_start = ed.redo_size;  // where this batch lands on redo
    for (uint8_t i = 0; i < batch_len; ++i) {
        ed.redo_buf[redo_start + i] = ed.undo_buf[m + i];
    }
    ed.redo_size = static_cast<uint8_t>(redo_start + batch_len);
    if (ed.redo_marks_count < kUndoMarksMax) {
        ed.redo_marks[ed.redo_marks_count++] = redo_start;
    }
    // Apply the OLD state and restore per-step originals + loop length.
    // A length-only entry (sentinel index) changes just the loop length and
    // touches no step; it also suppresses the cursor jump for that batch.
    Pattern& p = state_.active_pattern();
    bool len_only = false;
    for (uint8_t i = 0; i < batch_len; ++i) {
        const auto& e = ed.undo_buf[m + i];
        if (e.index == kLenOnlyIndex) {
            len_only = true;
            continue;
        }
        p.steps[e.index] = e.old_step;
        ed.prev_notes[e.index] = e.old_prev;
        p.length = e.len_before;  // last entry in the batch sets the length
    }
    if (len_only) {
        p.length = ed.undo_buf[m].len_before;
    }
    ed.undo_size = m;
    ed.undo_marks_count = static_cast<uint8_t>(ed.undo_marks_count - 1);
    // Move the cursor to the first affected step for visibility.
    if (batch_len > 0 && !len_only) {
        const auto& first = ed.undo_buf[m];
        ed.cur = ed.selected = first.index;
        state_.runtime.current_step = first.index;
        ed.page = static_cast<uint8_t>(first.index / 16);
    }
    return true;
}

bool AppLoop::ed_redo() {
    auto& ed = state_.editor;
    if (ed.redo_marks_count == 0 || ed.redo_size == 0) {
        return false;
    }
    const uint8_t m = ed.redo_marks[ed.redo_marks_count - 1];
    const uint8_t batch_len = static_cast<uint8_t>(ed.redo_size - m);
    const uint8_t undo_start = ed.undo_size;  // where this batch lands on undo
    for (uint8_t i = 0; i < batch_len; ++i) {
        ed.undo_buf[undo_start + i] = ed.redo_buf[m + i];
    }
    ed.undo_size = static_cast<uint8_t>(undo_start + batch_len);
    if (ed.undo_marks_count < kUndoMarksMax) {
        ed.undo_marks[ed.undo_marks_count++] = undo_start;
    }
    Pattern& p = state_.active_pattern();
    bool len_only = false;
    for (uint8_t i = 0; i < batch_len; ++i) {
        const auto& e = ed.redo_buf[m + i];
        if (e.index == kLenOnlyIndex) {
            len_only = true;
            continue;
        }
        p.steps[e.index] = e.new_step;
        ed.prev_notes[e.index] = e.new_prev;
        p.length = e.len_after;  // last entry in the batch sets the length
    }
    if (len_only) {
        p.length = ed.redo_buf[m].len_after;
    }
    ed.redo_size = m;
    ed.redo_marks_count = static_cast<uint8_t>(ed.redo_marks_count - 1);
    if (batch_len > 0 && !len_only) {
        const auto& first = ed.redo_buf[m];
        ed.cur = ed.selected = first.index;
        state_.runtime.current_step = first.index;
        ed.page = static_cast<uint8_t>(first.index / 16);
    }
    return true;
}

void AppLoop::editor_shortcut(uint8_t key, uint32_t now_ms) {
    auto& ed = state_.editor;
    Pattern& p = state_.active_pattern();
    const std::size_t len =
        std::max<std::size_t>(1, std::min<std::size_t>(p.length, kStepCountMax));
    const std::size_t page_len = std::min<std::size_t>(
        16, len - static_cast<std::size_t>(ed.page) * 16);
    const std::size_t start = static_cast<std::size_t>(ed.page) * 16;

    switch (key) {
        case kKeyCopy: {
            // When a range is selected, copy exactly that range. Otherwise fall
            // back to the whole visible page so the shortcut still works as a
            // quick whole-page grab.
            ed.clip_len = 0;
            if (ed.sel_active) {
                const int lo = std::max<int>(0, ed.sel_a);
                const int hi =
                    std::min<int>(static_cast<int>(len) - 1, ed.sel_b);
                for (int i = lo; i <= hi; ++i) {
                    ed.clip[ed.clip_len++] = p.steps[static_cast<std::size_t>(i)];
                }
                ed.clip_anchor = static_cast<uint8_t>(lo);
            } else {
                for (std::size_t i = 0; i < page_len; ++i) {
                    ed.clip[i] = p.steps[start + i];
                }
                ed.clip_len = static_cast<uint8_t>(page_len);
                ed.clip_anchor = static_cast<uint8_t>(start);
            }
            set_hint(kHintCopy, now_ms);
            break;
        }
        case kKeyPaste: {
            // Paste begins at the step the cursor currently sits on, so a copied
            // range can be laid down anywhere rather than only at the page edge.
            const std::size_t pstart =
                std::min<std::size_t>(ed.cur, len - 1);
            ed_undo_begin();
            for (uint8_t k = 0; k < ed.clip_len; ++k) {
                const std::size_t idx = pstart + k;
                if (idx >= len) {
                    break;
                }
                const Step old = p.steps[idx];
                const int16_t old_prev = ed.prev_notes[idx];
                // Paste clobbers the target step; remember its current note as
                // the "original" until undo clears it, so +/- mirrors the paste.
                if (ed.prev_notes[idx] < 0) {
                    ed.prev_notes[idx] = static_cast<int16_t>(p.steps[idx].notes[0]);
                }
                p.steps[idx] = ed.clip[k];
                ed_undo_record(static_cast<uint8_t>(idx), old, old_prev);
            }
            set_hint(kHintPaste, now_ms);
            break;
        }
        case kKeyDup: {
            // Duplicate (copy + paste at once): grab the current page into the
            // clipboard and append it onto the page AFTER this one. Growing
            // the loop length to keep the copy in the pattern is undoable.
            for (std::size_t i = 0; i < page_len; ++i) {
                ed.clip[i] = p.steps[start + i];
            }
            ed.clip_len = static_cast<uint8_t>(page_len);
            const std::size_t dst = start + 16;  // one page further on
            if (dst >= kStepCountMax) {
                break;  // no room to duplicate
            }
            const std::size_t need =
                dst + static_cast<std::size_t>(ed.clip_len);
            std::size_t new_len = len;
            for (int kIndex = 0; kIndex < 5; ++kIndex) {
                if (static_cast<std::size_t>(kPatternLens[kIndex]) >= need) {
                    new_len = static_cast<std::size_t>(kPatternLens[kIndex]);
                    break;
                }
            }
            if (new_len > kStepCountMax) {
                new_len = kStepCountMax;
            }
            ed_undo_begin();
            if (new_len > len) {
                p.length = static_cast<uint8_t>(new_len);  // captured as len_after
            }
            for (uint8_t k = 0; k < ed.clip_len; ++k) {
                const std::size_t idx = dst + k;
                if (idx >= kStepCountMax) {
                    break;
                }
                const Step old = p.steps[idx];
                const int16_t old_prev = ed.prev_notes[idx];
                if (ed.prev_notes[idx] < 0) {
                    ed.prev_notes[idx] = static_cast<int16_t>(p.steps[idx].notes[0]);
                }
                p.steps[idx] = ed.clip[k];
                ed_undo_record(static_cast<uint8_t>(idx), old, old_prev);
            }
            set_hint(kHintDup, now_ms);
            break;
        }
        case kKeyUndo: {
            if (ed_undo()) {
                set_hint(kHintUndo, now_ms);
            }
            break;
        }
        case kKeyRedo: {
            if (ed_redo()) {
                set_hint(kHintRedo, now_ms);
            }
            break;
        }
        default:
            break;
    }
}

void AppLoop::update_idle_screensaver(uint32_t now_ms, bool fresh_input) {
    auto& runtime = state_.runtime;
    if (runtime.test_mode) {
        return;
    }
    const uint32_t idle = runtime.click.idle_ms;
    if (idle == 0) {
        return;  // screensaver disabled
    }
    const bool anim = (runtime.screen_mode == ScreenMode::Animation);
    if (!anim) {
        if (now_ms - last_input_ms_ >= idle) {
            screensaver_origin_ = runtime.screen_mode;
            runtime.screen_mode = ScreenMode::Animation;
            screensaver_active_ = true;
            // Every entry starts from a fresh random stage of the scene.
            renderer_.restart_animation(now_ms ^ (last_input_ms_ * 31u + 0x9E37u));
            menu_.rebuild();
            ui_dirty_ = true;
        }
        return;
    }
    // In the animation, any FRESH input edge wakes back to the origin screen.
    // The edge (not the idle window) is the trigger: a manually launched run
    // re-anchors last_input_ms_ at adoption, and the activating click itself
    // must not undo it — hence the short post-entry suppression window.
    if (screensaver_active_ && fresh_input && now_ms >= suppress_wake_until_) {
        screensaver_active_ = false;
        runtime.screen_mode = screensaver_origin_;
        menu_.rebuild();
        ui_dirty_ = true;
    }
}

void AppLoop::update_beat(uint32_t now_ms) {
    // Transport metronome independent of the (not yet running) pattern
    // sequencer: while playing, advance runtime.beat (0..3) every quarter at
    // the current tempo so the status dial always rotates during a live arp or
    // the random loop. When stopped or in slave-sync mode the marker freezes
    // (Slave follows the host's F8 ticks through update_midi_clock directly).
    const TimingCfg& t = state_.active_pattern().timing;
    if (t.clock == ClockSync::Slave) {
        state_.runtime.beat = 0;
        beat_tick_ms_ = 0;
        return;
    }
    if (!state_.runtime.playing) {
        state_.runtime.beat = 0;
        beat_tick_ms_ = 0;
        return;
    }
    const int bpm = static_cast<int>(t.bpm);
    if (bpm <= 0) {
        return;
    }
    // One quarter note lasts 60000/bpm ms; the dial shows four quarters, so
    // one sector advances every 60000/bpm ms (a full bar = four quarters).
    const uint32_t quarter_ms = static_cast<uint32_t>(60000 / bpm);
    if (beat_tick_ms_ == 0) {
        beat_tick_ms_ = now_ms;
        state_.runtime.beat = 0;
        return;
    }
    const uint32_t elapsed = now_ms - beat_tick_ms_;
    if (quarter_ms > 0 && elapsed >= quarter_ms) {
        // Step through as many quarters as fit into the elapsed time so a
        // long stall between repaints does not freeze the dial position.
        state_.runtime.beat =
            static_cast<uint8_t>((state_.runtime.beat + elapsed / quarter_ms) % 4);
        beat_tick_ms_ = now_ms - (elapsed % quarter_ms);
        ui_dirty_ = true;
    }
}

[[noreturn]] void AppLoop::run() {
    while (true) {
        tud_task();
        const uint32_t now_ms = to_ms_since_boot(get_absolute_time());

        const uint32_t raw = shift_.read_all();

        process_functional(raw, now_ms);
        process_notes(raw, now_ms);
        process_joystick(raw, now_ms);
        update_midi_clock(now_ms);

        // Rotate the status-dial metronome after MIDI Clock sync has had a
        // chance to set runtime.playing (slave Start/Stop), so stop/play never
        // leave the dial stuck on a filled sector.
        update_beat(now_ms);

        // Activity detection runs BEFORE the screensaver check so a fresh press
        // during the auto-entered animation wakes it back to the interactive
        // screen within this very loop, instead of one frame later.
        constexpr uint32_t kUnusedMask = 0x00300000u;  // raw bits 20,21
        const uint32_t raw_masked = raw & ~kUnusedMask;
        const bool joy_motion = joy_.direction() != Direction::Center;
        const bool activity = (raw_masked != (last_raw_ & ~kUnusedMask)) ||
                              joy_motion || joy_.button();
        if (activity) {
            last_input_ms_ = now_ms;
        }

        // Idle screensaver: Animate after kScreensaverIdleMs without input.
        update_idle_screensaver(now_ms, activity);

        // Raw note-key image for the test screen: bit i = key i pressed.
        state_.runtime.note_bits = static_cast<uint16_t>(~raw & 0xFFFFu);

        // Test screen watch: draw regardless of menu motion.
        if (state_.runtime.test_mode != last_test_mode_) {
            menu_.rebuild();
            last_test_mode_ = state_.runtime.test_mode;
            if (state_.runtime.test_mode) {
                mode_.all_notes_off();
            }
            ui_dirty_ = true;
        }

        // Persist ClickSettings changes to flash a moment after the last edit.
        const ClickSettings& c = state_.runtime.click;
        if (c.debounce_ms != saved_click_.debounce_ms ||
            c.double_ms != saved_click_.double_ms ||
            c.long_ms != saved_click_.long_ms ||
            c.idle_ms != saved_click_.idle_ms) {
            if ((now_ms - last_persist_edit_ms_) >= kPersistSaveDelayMs) {
                persist_save_click(c);
                saved_click_ = c;
                last_persist_edit_ms_ = now_ms;
            }
        } else {
            last_persist_edit_ms_ = now_ms;
        }

        // Activity LED: lit while any physical input differs from the previous
        // sampled 24-bit image (any pressed or released key/function button).
        // The non-connected chip3 bits 4-5 float, so they are masked out to avoid
        // the LED strobing from mere electrical noise. (activity already holds
        // the current-frame comparison computed before the screensaver check.)
        gpio_put(BoardPins::kLed, activity);
        last_raw_ = raw;

        mode_.tick(now_ms);
        // Live-note readout: repaint when the arp/random advanced the sounding
        // note even though no physical input happened.
        if (mode_.take_ui_dirty()) {
            ui_dirty_ = true;
        }

        // The edit cursor, the step that accepts note keys and the playback
        // playhead are ONE object while the editor is on screen: they all live
        // at `runtime.current_step`. When Play advances (or note keys place a
        // note) the marker moves with the playhead; when it stops, editing
        // resumes at exactly the step where it stopped.
        if (state_.runtime.screen_mode == ScreenMode::Edit) {
            auto& ed = state_.editor;
            const uint8_t st = static_cast<uint8_t>(std::min<int>(
                state_.runtime.current_step, kStepCountMax - 1));
            ed.cur = st;
            ed.selected = st;
            ed.has_selected = true;
            ed.page = static_cast<uint8_t>(st / 16);
            // Expire the transient hotkey confirmation.
            if (ed.hint != kHintNone && now_ms >= ed.hint_until_ms) {
                ed.hint = kHintNone;
                ui_dirty_ = true;
            }
        }

        // The QUICK row set depends on the play mode; when it changes (via the
        // Mode cell) rebuild the menu and silence anything still sounding.
        if (state_.runtime.mode != last_mode_) {
            mode_.all_notes_off();
            mode_.random_loop_stop();
            mode_.gen_stop();
            mode_.capture_transport_stop();
            menu_.rebuild();
            last_mode_ = state_.runtime.mode;
            ui_dirty_ = true;
        }

        // Animation entered by hand (Timing -> Anim): adopt the switch into the
        // screensaver contract, so any input returns to this very screen.
        // last_input_ms_ is re-anchored and a short suppression window is set
        // because the activating click itself is fresh input — without this
        // the animation would wake instantly.
        if (state_.runtime.screen_mode != last_screen_mode_) {
            // Leaving the EDIT screen drops any SELECT sub-mode and range: it
            // is editor-local. (Entering EDIT with an old range would leak a
            // stale highlight into a freshly reopened editor.)
            if (last_screen_mode_ == ScreenMode::Edit &&
                state_.runtime.screen_mode != ScreenMode::Edit) {
                state_.editor.sel_mode = false;
                state_.editor.sel_active = false;
                // Don't carry a running slot transport out of the editor:
                // silence/stop it so no note hangs and the next re-entry starts
                // clean. (The EDIT Play path is slot audition; outside the
                // editor the same flag means something else.)
                mode_.pattern_stop();
                mode_.capture_transport_stop();
                state_.runtime.playing = false;
            }
            if (state_.runtime.screen_mode == ScreenMode::Animation &&
                !screensaver_active_) {
                screensaver_origin_ = last_screen_mode_;
                screensaver_active_ = true;
                last_input_ms_ = now_ms;
                suppress_wake_until_ = now_ms + kManualAnimWakeGuardMs;
                renderer_.restart_animation(now_ms ^ (last_input_ms_ * 31u + 0x9E37u));
                menu_.rebuild();
                ui_dirty_ = true;
            }
            last_screen_mode_ = state_.runtime.screen_mode;
        }

        // Triplet filter changed: the LEN cell bounds and labels come from the
        // filtered division list, so rebuild the menu to refresh them.
        if (state_.active_pattern().random.len_triplets != last_len_triplets_) {
            last_len_triplets_ = state_.active_pattern().random.len_triplets;
            menu_.rebuild();
            ui_dirty_ = true;
        }

        // Randomize DETAIL -> Regen: re-roll the PTRN slot, keep playing.
        if (state_.runtime.regen_req) {
            state_.runtime.regen_req = false;
            if (state_.runtime.mode == PlayMode::RandomPattern) {
                mode_.gen_regen_now(now_ms);
            }
        }

        // Keep the Animation screen advancing at 12 FPS even when no input.
        if (state_.runtime.screen_mode == ScreenMode::Animation &&
            (now_ms - last_anim_ms_) >= kAnimFrameMs) {
            last_anim_ms_ = now_ms;
            ui_dirty_ = true;
        }
        if (ui_dirty_) {
            renderer_.render(state_, menu_);
            ui_dirty_ = false;
        }
        if ((now_ms - last_flush_ms_) >= kFlushMs) {
            display_.flush();
            last_flush_ms_ = now_ms;
        }

        sleep_ms(1);
    }
}

}  // namespace drom
