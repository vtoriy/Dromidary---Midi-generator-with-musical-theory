#pragma once

#include "menu/menu_engine.hpp"
#include "menu/renderer.hpp"
#include "mode/mode_engine.hpp"
#include "platform/display_sh1106.hpp"
#include "platform/joystick.hpp"
#include "platform/shift165.hpp"
#include "platform/usb_midi.hpp"
#include "state/state.hpp"

namespace drom {

class AppLoop {
public:
    void init();
    [[noreturn]] void run();

private:
    void process_functional(uint32_t raw, uint32_t now_ms);
    void process_notes(uint32_t raw, uint32_t now_ms);
    void process_joystick(uint32_t raw, uint32_t now_ms);
    void update_midi_clock(uint32_t now_ms);
    void update_beat(uint32_t now_ms);
    void update_idle_screensaver(uint32_t now_ms, bool fresh_input);

    // Pattern editor (ScreenMode::Edit) input handlers.
    void editor_move(int delta);
    void editor_tilt(Direction dir, bool shift);
    void editor_cycle_field();
    void editor_erase_step();   // Rest: clear the focused step
    void editor_erase_page();   // Shift+Rest: clear the visible 16-step page
    void editor_select_toggle();  // Shift+click: enter/leave the SELECT sub-mode
    void editor_shortcut(uint8_t key, uint32_t now_ms);  // Shift + note-key hotkeys

    // Undo/redo (command-based, batched). Callers snapshot the OLD state, then
    // modify the pattern/prev_notes, then ed_undo_record(...) reads the NEW
    // state back in. ed_undo_begin() opens a batch (one user action may touch
    // many steps). Any new batch clears the redo log.
    void ed_undo_begin();
    void ed_undo_record(uint8_t index, const Step& old_step, int16_t old_prev);
    bool ed_undo();
    bool ed_redo();

    void set_hint(uint8_t value, uint32_t now_ms);

    bool is_pressed(uint32_t raw, uint8_t bit) const;
    uint8_t button_to_note(uint8_t index) const;

    void update_func(bool raw_bits[8], uint32_t now_ms);
    bool fn_pressed(uint8_t bit) const;
    bool fn_edge(uint8_t bit) const;
    bool fn_fell(uint8_t bit) const;

    AppState state_ {};
    Shift165 shift_ {};
    UsbMidi midi_ {};
    Joystick joy_;
    DisplaySh1106 display_ {};
    ModeEngine mode_ {};
    MenuEngine menu_ {};
    MenuRenderer renderer_{&display_};

    std::array<uint8_t, kMaxHeldKeys> note_debounce_ {};
    std::array<bool, kMaxHeldKeys> key_state_ {};  // confirmed (debounced) state
    std::array<bool, kMaxHeldKeys> note_held_ {};

    // Debounced functional-button inputs. The debouncer operates on the raw
    // 24-bit read: it keeps per-chip3-bit start timestamps (index 0..7 = raw
    // bits 16..23) and stores the confirmed image as a copy of the raw word
    // with only those functional bits updated. fn_pressed/fn_edge take RAW bit
    // indices (16..23), exactly like the note keys use raw bits 0..15.
    std::array<uint32_t, 8> func_pending_start_ {};
    uint32_t func_stable_ {0};
    uint32_t func_prev_ {0};
    PlayMode last_mode_ {PlayMode::MidiKeyboard};
    bool last_test_mode_ {false};
    uint32_t last_raw_ {0};  // previous 24-bit read, for the activity LED

    // Last value persisted to flash (for debounced ClickSettings saves).
    ClickSettings saved_click_ {};
    uint32_t last_persist_edit_ms_ {0};  // wall-clock anchor for the save debounce;

    uint32_t last_joy_tilt_ms_ {0};
    Direction last_joy_dir_ {Direction::Center};
    uint32_t joy_hold_ms_ {0};  // start of the current same-direction hold (for tilt acceleration)
    uint32_t joy_suppress_tilt_until_ {0};  // ignore tilts until this time (post click / screen entry)
    bool joy_btn_prev_ {false};
    uint32_t joy_btn_press_ms_ {0};
    uint32_t joy_btn_last_click_ms_ {0};
    bool joy_btn_long_ {false};
    uint8_t joy_btn_debounce_ {0};
    bool joy_btn_stable_ {false};

    uint32_t last_flush_ms_ {0};
    uint32_t last_anim_ms_ {0};
    bool ui_dirty_ {true};

    // MIDI Clock sync state.
    // Master: 24 ppqn ticker against runtime.playing.
    // Master clock accumulator: elapsed time in 1/64 ms units; an F8 goes out
    // every 160000/bpm units (= 60000/bpm/24 ms). Elapsed-delta based, so main
    // loop latency cannot drift the tempo.
    uint32_t clock_last_now_ms_ {0};
    uint32_t clock_acc_64th_ {0};
    bool master_started_ {false};     // start/continue already sent for this run
    // Live transport-metronome state: advances runtime.beat (0..3) at quarter
    // tempo while playing, so the status dial rotates without a pattern run.
    uint32_t beat_tick_ms_ {0};       // wall-clock anchor for the next quarter

    // Idle screensaver: auto-switches Quick/Full to the Animation screen after
    // kScreensaverIdleMs of no user input, and returns to the last interactive
    // screen on the first press afterwards.
    uint32_t last_input_ms_ {0};
    ScreenMode screensaver_origin_ {ScreenMode::Quick};
    bool screensaver_active_ {false};
    ScreenMode last_screen_mode_ {ScreenMode::Quick};  // detects manual Anim entry
    uint32_t suppress_wake_until_ {0};  // ignore wake edges right after manual entry
    bool last_len_triplets_ {false};    // rebuild menu when the triplet filter flips
    // Slave: received-tick rate estimator. The interval between two F8s is
    // averaged into clock_avg_interval_ and converted to BPM for the pattern.
    uint32_t clock_last_rx_ms_ {0};
    uint32_t clock_sample_interval_ {0};  // last F8..F8 gap (ms)
    uint32_t clock_avg_interval_ {0};     // smoothed gap (ms); 0 = no sample yet
    bool slave_running_ {false};          // 0xFA/0xFB seen, waiting for F8 stream
};

}  // namespace drom