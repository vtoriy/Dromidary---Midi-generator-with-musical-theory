#pragma once

#include "../engine/arpeggiator.hpp"
#include "../engine/chord_builder.hpp"
#include "../platform/usb_midi.hpp"
#include "../state/state.hpp"

namespace drom {

// MIDI keyboard mode logic: live notes through key filter -> chord -> arp,
// polyphonic arpeggio over all held keys, latch support, held-note tracking.
class ModeEngine {
public:
    void init(UsbMidi* midi, AppState* state);

    void note_on(uint8_t chip_idx, uint8_t raw_note, uint32_t now_ms);
    void note_off(uint8_t chip_idx, uint32_t now_ms);

    // Advance the live arpeggio scheduler; call from the main loop every tick.
    void tick(uint32_t now_ms);

    // Silence everything (arp, latch, held notes).
    void all_notes_off();

    void on_arp_config_changed(uint32_t now_ms);

    // Continuous RandomNote mode: started by Play or by a first key press and
    // kept running until RandomNote is left or Play is pressed again. A fresh
    // random note (round a fixed anchor, filtered into the current scale) is
    // triggered at the arp rate for as long as the loop is active.
    void random_loop_start(uint8_t anchor_octave, uint32_t now_ms);
    void random_loop_stop();
    bool random_loop_running() const { return random_loop_; }

private:
    bool arp_enabled() const;
    bool latch_enabled() const;
    bool any_chip_in_arp() const;
    bool any_latched() const;
    uint8_t note_set_for_chip(uint8_t chip_idx) const;
    uint32_t arp_fingerprint() const;
    void rebuild_arp(uint32_t now_ms, bool align_next_step = false);
    void stop_arp();
    void advance_arp(uint32_t now_ms);
    void schedule_next_step(uint32_t now_ms);
    void advance_random(uint32_t now_ms);
    void release_chip(uint8_t chip_idx);
    void check_config(uint32_t now_ms);
    uint8_t pick_random_note(uint8_t anchor);
    // True while any note is still sounding (held/latched/arp/random loop).
    bool any_active_input() const;
    // Sync runtime.show_note with any_active_input(); call after note_off etc.
    void refresh_show_note();

    UsbMidi* midi_ {nullptr};
    AppState* state_ {nullptr};

    // chip_idx -> raw note participating in the polyphonic arp.
    std::array<uint8_t, kMaxHeldKeys> arp_notes_ {};
    bool chip_in_arp_[kMaxHeldKeys] {};

    // chip_idx -> sounding NoteSet for non-arp polyphony.
    std::array<NoteSet, kMaxHeldKeys> held_set_ {};
    bool chip_held_[kMaxHeldKeys] {};

    // latch-latched chips (mono-replace when arp + latch).
    bool latched_[kMaxHeldKeys] {};

    std::array<uint8_t, kMaxArpNotes> arp_seq_ {};
    uint8_t arp_seq_count_ {0};
    uint8_t arp_index_ {0};
    uint32_t next_arp_ms_ {0};
    uint32_t last_step_ms_ {0};
    uint8_t current_arp_note_ {0};
    bool arp_active_ {false};

    // Fingerprint of the config that produced the current arp sequence; used
    // to skip rebuilds when a menu navigation didn't actually change anything.
    uint32_t last_rebuild_fp_ {0};

    SimpleRng rng_ {0xD0A4A220u};

    // RandomNote continuous generator state.
    bool random_loop_ {false};
    uint8_t random_anchor_ {60};  // GC4
    uint8_t last_random_note_ {0};
    uint32_t next_random_ms_ {0};
};

}  // namespace drom
