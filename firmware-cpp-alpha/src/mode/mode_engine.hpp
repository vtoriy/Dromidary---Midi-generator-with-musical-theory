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

    // True while any note is still sounding (held/latched/arp/random loop);
    // used by the Play transport to decide whether a press should silence it.
    bool any_active_input() const;

    // Continuous RandomNote mode: started by Play or by a first key press and
    // kept running until RandomNote is left or Play is pressed again. A fresh
    // random note (round a fixed anchor, filtered into the current scale) is
    // triggered at the arp rate for as long as the loop is active.
    void random_loop_start(uint8_t anchor_octave, uint32_t now_ms);
    void random_loop_stop();
    bool random_loop_running() const { return random_loop_; }

    // RandomPattern (GEN): a random sequence is generated once into the active
    // pattern slot and looped as a duration chain — every event lasts its drawn
    // LEN division and the next onset lands on the previous gate end. A key
    // press regenerates (same key toggles off), Play starts/stops the loop.
    void gen_start(uint8_t anchor_note, uint32_t now_ms);  // regenerate + play
    void gen_toggle_play();                                // Play without regen
    void gen_regen_now(uint32_t now_ms);                   // re-roll, keep playing
    void gen_stop();
    bool gen_running() const { return gen_playing_; }

    // Polled by the main loop: true when the live note label should be
    // repainted although no physical input happened (arp step advance,
    // random transitions). Take-and-clear semantics.
    bool take_ui_dirty();

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
    void release_chip(uint8_t chip_idx, uint32_t now_ms);
    void check_config(uint32_t now_ms);
    uint8_t pick_random_note(uint8_t anchor);
    // REP helper: the KEY note mapped into the PITCH range by pitch class —
    // only the note name is kept, the octave snaps to the nearest in-range
    // occurrence (C0 with range C1-C2 becomes C1).
    uint8_t anchor_in_range() const;
    // Sync runtime.show_note with any_active_input(); call after note_off etc.
    void refresh_show_note();

    // -- Delayed note events (gate attack/release, voicing strum, timing) -----
    struct PendingEvent {
        uint32_t at_ms {0};
        uint8_t note {0};
        bool on {false};
        bool active {false};
    };
    static constexpr uint8_t kMaxPendingEvents = 48;
    std::array<PendingEvent, kMaxPendingEvents> pending_ {};
    uint8_t pending_cursor_ {0};

    uint32_t gate_attack_ms() const;
    uint32_t gate_release_ms() const;
    void clear_pending();
    void cancel_pending(uint8_t note, bool on);
    void schedule_pending(uint32_t at_ms, uint8_t note, bool on);
    void process_pending(uint32_t now_ms);
    uint32_t next_arp_onset(const Pattern& p, uint32_t now_ms, uint32_t interval);

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

    // RandomPattern (GEN) player state.
    void gen_regenerate(uint8_t anchor);
    void gen_advance(uint32_t now_ms);
    bool gen_playing_ {false};
    uint8_t gen_pos_ {0};          // event index within the slot
    uint8_t gen_anchor_ {60};
    uint8_t gen_last_note_ {0};    // sounding event note (for chained offs)
    uint32_t gen_next_ms_ {0};

    // Set whenever the note readout changed outside of input handling.
    bool ui_repaint_ {false};
};

}  // namespace drom
