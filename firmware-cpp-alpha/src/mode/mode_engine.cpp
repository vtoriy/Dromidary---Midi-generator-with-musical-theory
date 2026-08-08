#include "mode_engine.hpp"

#include "../engine/key_filter.hpp"
#include "../engine/midi_chain.hpp"

namespace drom {

namespace {
constexpr int kCapNote = 0xFF;

bool arp_mode(PlayMode m) {
    return m == PlayMode::MidiKeyboard || m == PlayMode::MidiFilter;
}

uint32_t fp_mix(uint32_t h, uint32_t v) {
    return (h ^ v) * 16777619u;
}
}  // namespace

void ModeEngine::init(UsbMidi* midi, AppState* state) {
    midi_ = midi;
    state_ = state;
    all_notes_off();
}

bool ModeEngine::latch_enabled() const {
    return state_->active_pattern().arp.latch;
}

bool ModeEngine::arp_enabled() const {
    return state_->active_pattern().arp.enabled && arp_mode(state_->runtime.mode);
}

bool ModeEngine::any_chip_in_arp() const {
    for (uint8_t c = 0; c < kMaxHeldKeys; ++c) {
        if (chip_in_arp_[c]) {
            return true;
        }
    }
    return false;
}

bool ModeEngine::any_latched() const {
    for (uint8_t c = 0; c < kMaxHeldKeys; ++c) {
        if (latched_[c]) {
            return true;
        }
    }
    return false;
}

void ModeEngine::all_notes_off() {
    stop_arp();
    random_loop_stop();
    if (state_ != nullptr) {
        state_->runtime.show_note = false;
    }
    for (uint8_t c = 0; c < kMaxHeldKeys; ++c) {
        chip_in_arp_[c] = false;
        chip_held_[c] = false;
        latched_[c] = false;
        if (held_set_[c].count > 0 && midi_) {
            for (uint8_t i = 0; i < held_set_[c].count; ++i) {
                midi_->note_off(held_set_[c].notes[i]);
            }
        }
        held_set_[c].count = 0;
    }
}

void ModeEngine::release_chip(uint8_t chip_idx) {
    latched_[chip_idx] = false;
    chip_in_arp_[chip_idx] = false;
    if (chip_held_[chip_idx] && midi_) {
        for (uint8_t i = 0; i < held_set_[chip_idx].count; ++i) {
            midi_->note_off(held_set_[chip_idx].notes[i]);
        }
        held_set_[chip_idx].count = 0;
        chip_held_[chip_idx] = false;
    }
}

bool ModeEngine::any_active_input() const {
    if (arp_active_ || random_loop_) {
        return true;
    }
    for (uint8_t c = 0; c < kMaxHeldKeys; ++c) {
        if (chip_held_[c] || chip_in_arp_[c] || latched_[c]) {
            return true;
        }
    }
    return false;
}

void ModeEngine::refresh_show_note() {
    if (state_ != nullptr) {
        state_->runtime.show_note = any_active_input();
    }
}

void ModeEngine::stop_arp() {
    if (current_arp_note_ != kCapNote && midi_) {
        midi_->note_off(current_arp_note_);
    }
    current_arp_note_ = kCapNote;
    arp_active_ = false;
    arp_seq_count_ = 0;
    last_step_ms_ = 0;
}

void ModeEngine::schedule_next_step(uint32_t now_ms) {
    const Pattern& p = state_->active_pattern();
    const uint16_t interval = arp_interval_ms(p.arp, p.bpm);
    // Align the restart to the next step boundary of the arp grid (a multiple
    // of the selected quantization), so the rhythm stays locked to the beat.
    const uint32_t t0 = (last_step_ms_ != 0) ? last_step_ms_ : now_ms;
    const uint32_t since = (now_ms >= t0) ? (now_ms - t0) : 0;
    const uint32_t n = since / interval + 1;
    next_arp_ms_ = t0 + n * interval;
}

void ModeEngine::advance_arp(uint32_t now_ms) {
    if (!arp_active_ || arp_seq_count_ == 0) {
        return;
    }
    if (now_ms < next_arp_ms_) {
        return;
    }
    if (current_arp_note_ != kCapNote && midi_) {
        midi_->note_off(current_arp_note_);
    }
    const uint8_t note = arp_seq_[arp_index_ % arp_seq_count_];
    arp_index_ = static_cast<uint8_t>((arp_index_ + 1) % arp_seq_count_);
    // Steady hold: when the next step is the same note already sounding, do not
    // re-trigger it (that retrigger caused the interrupted, percussive sound
    // while a key was held). Keep it ringing until a genuinely different note.
    if (note != current_arp_note_) {
        if (current_arp_note_ != kCapNote && midi_) {
            midi_->note_off(current_arp_note_);
        }
        current_arp_note_ = note;
        if (midi_) {
            midi_->note_on(note, 100);
        }
        if (state_ != nullptr) {
            state_->runtime.last_note = note;
            state_->runtime.show_note = true;
        }
    }
    last_step_ms_ = now_ms;
    const Pattern& p = state_->active_pattern();
    next_arp_ms_ = now_ms + arp_interval_ms(p.arp, p.bpm);
}

// Hash of every config value that influences the live arp sequence.
uint32_t ModeEngine::arp_fingerprint() const {
    const Pattern& p = state_->active_pattern();
    uint32_t h = 2166136261u;
    h = fp_mix(h, p.arp.enabled ? 1u : 0u);
    h = fp_mix(h, p.arp.latch ? 1u : 0u);
    h = fp_mix(h, static_cast<uint32_t>(p.arp.rate_mode));
    h = fp_mix(h, p.arp.rate_note_index);
    h = fp_mix(h, p.arp.rate_ms);
    h = fp_mix(h, p.arp.range_semitones);
    h = fp_mix(h, p.arp.num_steps);
    h = fp_mix(h, static_cast<uint32_t>(p.arp.style));
    h = fp_mix(h, p.key_filter.enabled ? 1u : 0u);
    h = fp_mix(h, p.key_filter.root_note);
    h = fp_mix(h, static_cast<uint32_t>(p.key_filter.scale));
    h = fp_mix(h, static_cast<uint32_t>(p.key_filter.mode));
    h = fp_mix(h, p.chord.enabled ? 1u : 0u);
    h = fp_mix(h, static_cast<uint32_t>(p.chord.type));
    h = fp_mix(h, static_cast<uint32_t>(p.transpose.semitones));
    h = fp_mix(h, static_cast<uint32_t>(p.transpose.octaves));
    h = fp_mix(h, p.bpm);
    h = fp_mix(h, static_cast<uint32_t>(state_->runtime.mode));
    return h;
}

// Merge the note sets of all pressed/latched keys into one arp set.
void ModeEngine::rebuild_arp(uint32_t now_ms, bool align_next_step) {
    NoteSet merged {};
    bool seen[128] {};
    for (uint8_t c = 0; c < kMaxHeldKeys; ++c) {
        if (!chip_in_arp_[c]) {
            continue;
        }
        const NoteSet set = build_note_set(arp_notes_[c], state_->active_pattern());
        for (uint8_t i = 0; i < set.count; ++i) {
            if (!seen[set.notes[i]] && merged.count < merged.notes.size()) {
                seen[set.notes[i]] = true;
                merged.notes[merged.count++] = set.notes[i];
            }
        }
    }
    for (uint8_t i = 1; i < merged.count; ++i) {
        const uint8_t key = merged.notes[i];
        int8_t j = static_cast<int8_t>(i) - 1;
        while (j >= 0 && merged.notes[j] > key) {
            merged.notes[j + 1] = merged.notes[j];
            --j;
        }
        merged.notes[j + 1] = key;
    }

    last_rebuild_fp_ = arp_fingerprint();

    // In the aligned path the currently sounding note keeps ringing until the
    // next step boundary, where the new cycle begins; in the immediate path
    // (note press/release) cut it off right away for responsiveness.
    if (current_arp_note_ != kCapNote && midi_ && !align_next_step) {
        midi_->note_off(current_arp_note_);
        current_arp_note_ = kCapNote;
    }
    arp_active_ = false;
    arp_seq_count_ = 0;

    if (merged.count == 0) {
        if (current_arp_note_ != kCapNote && midi_) {
            midi_->note_off(current_arp_note_);
            current_arp_note_ = kCapNote;
        }
        return;
    }

    const Pattern& p = state_->active_pattern();
    build_arp_sequence(merged, p.arp, arp_seq_, arp_seq_count_);
    if (arp_seq_count_ == 0) {
        if (current_arp_note_ != kCapNote && midi_) {
            midi_->note_off(current_arp_note_);
            current_arp_note_ = kCapNote;
        }
        return;
    }
    arp_index_ = 0;
    arp_active_ = true;
    if (align_next_step) {
        schedule_next_step(now_ms);
    } else {
        next_arp_ms_ = 0;  // fire on the next advance (now_ms ignored)
    }
    (void)now_ms;
}

void ModeEngine::note_on(uint8_t chip_idx, uint8_t raw_note, uint32_t now_ms) {
    if (chip_idx >= kMaxHeldKeys) {
        return;
    }
    const PlayMode mode = state_->runtime.mode;
    if (mode == PlayMode::Pattern || mode == PlayMode::RandomPattern) {
        return;  // note keys ignored
    }

    const bool latch = latch_enabled();
    const bool arp = arp_enabled();

    // RandomNote: each pressed key raises the anchor octave of the continuous
    // random generator. The first press (or Play) starts the loop; it keeps
    // emitting random notes until RandomNote is left or Play is pressed again.
    if (mode == PlayMode::RandomNote) {
        if (latch) {
            latched_[chip_idx] = true;
        }
        // Record the pressed key so the status bar can show input > generated.
        if (state_ != nullptr) {
            state_->runtime.last_input_note = raw_note;
            state_->runtime.show_note = true;
        }
        if (!random_loop_) {
            random_loop_start(raw_note, now_ms);
        } else {
            random_anchor_ = raw_note;
        }
        if (random_loop_) {
            advance_random(now_ms);
        }
        return;
    }

    // Latch: pressing an already-latched chip toggles it off.
    if (latch && latched_[chip_idx]) {
        latched_[chip_idx] = false;
        if (arp) {
            chip_in_arp_[chip_idx] = false;
            if (any_chip_in_arp()) {
                rebuild_arp(now_ms);
            } else {
                stop_arp();
            }
        } else {
            release_chip(chip_idx);
        }
        return;
    }

    if (arp) {
        if (latch) {
            // Mono-latch: the new key replaces the previously latched one.
            for (uint8_t c = 0; c < kMaxHeldKeys; ++c) {
                if (c != chip_idx && latched_[c]) {
                    latched_[c] = false;
                    chip_in_arp_[c] = false;
                }
            }
            release_chip(chip_idx);
            arp_notes_[chip_idx] = raw_note;
            chip_in_arp_[chip_idx] = true;
            latched_[chip_idx] = true;
            rebuild_arp(now_ms);
        } else {
            arp_notes_[chip_idx] = raw_note;
            chip_in_arp_[chip_idx] = true;
            rebuild_arp(now_ms);
        }
        return;
    }

    // Non-arp live: send the note set immediately.
    const NoteSet set = build_note_set(raw_note, state_->active_pattern());
    if (set.count == 0) {
        return;
    }
    if (state_ != nullptr) {
        state_->runtime.last_note = set.notes[0];
        state_->runtime.show_note = true;
    }
    for (uint8_t i = 0; i < set.count; ++i) {
        midi_->note_on(set.notes[i], 100);
    }
    held_set_[chip_idx] = set;
    chip_held_[chip_idx] = true;
    if (latch) {
        latched_[chip_idx] = true;
    }
}

void ModeEngine::note_off(uint8_t chip_idx, uint32_t now_ms) {
    if (chip_idx >= kMaxHeldKeys) {
        return;
    }
    if (latched_[chip_idx]) {
        return;  // latch keeps the key/arp sounding after release
    }
    if (arp_enabled() && chip_in_arp_[chip_idx]) {
        chip_in_arp_[chip_idx] = false;
        if (any_chip_in_arp()) {
            rebuild_arp(now_ms);
        } else {
            stop_arp();
            refresh_show_note();
        }
        return;
    }
    release_chip(chip_idx);
    // A released key (and no other held/latched/arp note) hides the readout.
    refresh_show_note();
}

void ModeEngine::check_config(uint32_t now_ms) {
    // Arp disabled in runtime or current mode is not an arp mode -> stop arp.
    if (arp_active_ && !arp_enabled()) {
        stop_arp();
        for (uint8_t c = 0; c < kMaxHeldKeys; ++c) {
            chip_in_arp_[c] = false;
        }
    }
    // Latch turned off -> release latched keys.
    if (any_latched() && !latch_enabled()) {
        for (uint8_t c = 0; c < kMaxHeldKeys; ++c) {
            if (latched_[c]) {
                latched_[c] = false;
                if (arp_enabled()) {
                    chip_in_arp_[c] = false;
                } else {
                    release_chip(c);
                }
            }
        }
        if (arp_enabled()) {
            if (any_chip_in_arp()) {
                rebuild_arp(now_ms);
            } else {
                stop_arp();
            }
        }
    }
}

void ModeEngine::tick(uint32_t now_ms) {
    check_config(now_ms);
    if (arp_active_) {
        advance_arp(now_ms);
    }
    // RandomNote continuous generation runs on its own timer, independent of
    // the arp (which is disabled in that mode).
    if (random_loop_ && state_ != nullptr &&
        state_->runtime.mode == PlayMode::RandomNote) {
        advance_random(now_ms);
    } else if (random_loop_) {
        random_loop_stop();
    }
}

void ModeEngine::on_arp_config_changed(uint32_t now_ms) {
    check_config(now_ms);
    if (!arp_active_) {
        return;
    }
    // Only rebuild when an arp-relevant parameter actually changed (menu
    // navigation itself must not restart the arpeggio), and restart aligned
    // to the next step of the arp grid.
    const uint32_t fp = arp_fingerprint();
    if (fp != last_rebuild_fp_) {
        rebuild_arp(now_ms, true);
    }
}

void ModeEngine::random_loop_stop() {
    random_loop_ = false;
    if (last_random_note_ != 0 && midi_) {
        midi_->note_off(last_random_note_);
    }
    last_random_note_ = 0;
    next_random_ms_ = 0;
}

void ModeEngine::random_loop_start(uint8_t anchor, uint32_t now_ms) {
    random_loop_ = true;
    random_anchor_ = anchor;
    // First note goes out immediately; the rest follow every arp period.
    next_random_ms_ = now_ms;
    (void)advance_random(now_ms);
}

void ModeEngine::advance_random(uint32_t now_ms) {
    if (!random_loop_ || midi_ == nullptr || state_ == nullptr) {
        return;
    }
    if (now_ms < next_random_ms_) {
        return;
    }
    const Pattern& p = state_->active_pattern();
    const uint8_t picked = pick_random_note(random_anchor_);
    if (picked == last_random_note_ && last_random_note_ != 0) {
        // Keep the note ringing; only retrigger when a different tone is drawn.
        next_random_ms_ = now_ms + arp_interval_ms(p.arp, p.bpm);
        return;
    }
    if (last_random_note_ != 0 && midi_) {
        midi_->note_off(last_random_note_);
    }
    last_random_note_ = picked;
    if (midi_) {
        midi_->note_on(picked, 100);
    }
    if (state_ != nullptr) {
        state_->runtime.last_note = picked;
        state_->runtime.show_note = true;
    }
    next_random_ms_ = now_ms + arp_interval_ms(p.arp, p.bpm);
}

uint8_t ModeEngine::pick_random_note(uint8_t anchor) {
    const Pattern& p = state_->active_pattern();
    const KeyFilterCfg& kf = p.key_filter;
    const bool use_kf = kf.enabled && kf.scale != ScaleId::Off &&
                        static_cast<uint8_t>(kf.scale) < static_cast<uint8_t>(ScaleId::kCount);
    // A 3.5-octave band around the pressed key: center sits one octave below
    // the anchor so the generated note mostly stays in a pleasant range.
    const int center = static_cast<int>(anchor) - 12;
    for (int attempt = 0; attempt < 16; ++attempt) {
        const int midi = center + static_cast<int>(rng_.range(48));
        if (midi < 0 || midi > 127) {
            continue;
        }
        if (use_kf && !note_in_scale(static_cast<uint8_t>(midi), kf)) {
            continue;
        }
        return static_cast<uint8_t>(midi);
    }
    return anchor;
}

}  // namespace drom