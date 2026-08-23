#include "app_loop.hpp"

#include <cstring>

#include "pico/stdlib.h"
#include "tusb.h"

#include "persist.hpp"
#include "platform/board_pins.hpp"

namespace drom {

namespace {

constexpr uint8_t kMidiOctaveOffset = 2;
constexpr uint8_t kKeyDebounce = 5;
constexpr uint32_t kJoyRepeatMs = 190;
constexpr uint32_t kFlushMs = 40;
constexpr uint32_t kAnimFrameMs = 1000 / 12;  // 12 FPS
constexpr uint32_t kManualAnimWakeGuardMs = 400; // ignore wake edges after manual launch
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
        if (runtime.mode == PlayMode::RandomNote) {
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
        ui_dirty_ = true;
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
                    mode_.note_on(i, button_to_note(i), now_ms);
                    // A RandomNote key press starts the loop without the Play
                    // button, so reflect that transport in the status dial.
                    if (state_.runtime.mode == PlayMode::RandomNote && mode_.random_loop_running()) {
                        state_.runtime.playing = true;
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
        ui_dirty_ = true;
        mode_.on_arp_config_changed(now_ms);
    }
    if (!btn && joy_btn_prev_) {
        if (!joy_btn_long_) {
            // Rest + click resets the current value (QUICK cell or DETAIL/MAIN
            // item) to its last accepted value. Otherwise a double-click in
            // DETAIL/MAIN is the alternative reset, and a plain click is a
            // normal confirm/edit action.
            if (fn_pressed(kBtnRest)) {
                menu_.reset_value();
            } else if (menu_.editing_value_item() &&
                       (now_ms - joy_btn_last_click_ms_) < state_.runtime.click.double_ms) {
                menu_.reset_value();
            } else {
                menu_.press_short();
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
        if ((now_ms - last_joy_tilt_ms_) >= interval) {
            last_joy_tilt_ms_ = now_ms;
            const bool shift = fn_pressed(kBtnShift);
            for (uint32_t k = 0; k < taps; ++k) {
                if (menu_.editing_radial()) {
                    menu_.radial_select(direction_to_zone(dir));
                } else {
                    menu_.tilt(dir, shift);
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

        // The QUICK row set depends on the play mode; when it changes (via the
        // Mode cell) rebuild the menu and silence anything still sounding.
        if (state_.runtime.mode != last_mode_) {
            mode_.all_notes_off();
            mode_.random_loop_stop();
            mode_.gen_stop();
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