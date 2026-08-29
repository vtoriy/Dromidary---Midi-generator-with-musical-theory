#include "mode_engine.hpp"

#include <algorithm>

#include "../engine/key_filter.hpp"
#include "../engine/midi_chain.hpp"

namespace drom {

// Nearest kNoteLenDivs index for a measured duration in ms (defined below,
// used by the Pattern-mode note capture).
static uint8_t nearest_len_div(uint32_t dur_ms, uint16_t bpm);

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
    clear_pending();
}

void ModeEngine::release_chip(uint8_t chip_idx, uint32_t now_ms) {
    latched_[chip_idx] = false;
    chip_in_arp_[chip_idx] = false;
    if (chip_held_[chip_idx] && midi_) {
        const uint32_t release = gate_release_ms();
        const uint32_t off_at = now_ms + release;
        for (uint8_t i = 0; i < held_set_[chip_idx].count; ++i) {
            // A strummed/gate note whose Note On has not fired yet is dropped;
            // the fired ones get their (delayed) release.
            cancel_pending(held_set_[chip_idx].notes[i], true);
            schedule_pending(off_at, held_set_[chip_idx].notes[i], false);
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

bool ModeEngine::take_ui_dirty() {
    const bool v = ui_repaint_;
    ui_repaint_ = false;
    return v;
}

void ModeEngine::stop_arp() {
    if (current_arp_note_ != kCapNote && midi_) {
        midi_->note_off(current_arp_note_);
        cancel_pending(current_arp_note_, true);
    }
    current_arp_note_ = kCapNote;
    arp_active_ = false;
    arp_seq_count_ = 0;
    last_step_ms_ = 0;
}

void ModeEngine::schedule_next_step(uint32_t now_ms) {
    const Pattern& p = state_->active_pattern();
    const uint16_t interval = arp_interval_ms(p.arp, p.timing.bpm);
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
    const Pattern& p = state_->active_pattern();
    const uint16_t interval = arp_interval_ms(p.arp, p.timing.bpm);
    const uint32_t attack = gate_attack_ms();
    const uint32_t release = gate_release_ms();
    const bool legato = p.timing.legato;

    const uint8_t note = arp_seq_[arp_index_ % arp_seq_count_];
    arp_index_ = static_cast<uint8_t>((arp_index_ + 1) % arp_seq_count_);
    // Steady hold: when the next step is the same note already sounding, do not
    // re-trigger it (that retrigger caused the interrupted, percussive sound
    // while a key was held). Keep it ringing until a genuinely different note.
    if (note != current_arp_note_) {
        if (current_arp_note_ != kCapNote && midi_) {
            // Legato overlaps the tail of the previous note with the new one;
            // otherwise the previous note releases here (with gate release).
            if (legato) {
                schedule_pending(now_ms + interval, current_arp_note_, false);
            } else {
                schedule_pending(now_ms + release, current_arp_note_, false);
            }
        }
        current_arp_note_ = note;
        if (midi_) {
            schedule_pending(now_ms + attack, note, true);
        }
        if (state_ != nullptr) {
            state_->runtime.last_note = note;
            state_->runtime.show_note = true;
        }
        ui_repaint_ = true;
    }
    last_step_ms_ = now_ms;
    next_arp_ms_ = next_arp_onset(p, now_ms, interval);
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
    h = fp_mix(h, p.arp.distance_semitones);
    h = fp_mix(h, p.arp.steps);
    h = fp_mix(h, p.arp.cycle);
    h = fp_mix(h, static_cast<uint32_t>(p.arp.style));
    h = fp_mix(h, p.key_filter.enabled ? 1u : 0u);
    h = fp_mix(h, p.key_filter.root_note);
    h = fp_mix(h, static_cast<uint32_t>(p.key_filter.scale));
    h = fp_mix(h, static_cast<uint32_t>(p.key_filter.mode));
    h = fp_mix(h, p.chord.enabled ? 1u : 0u);
    h = fp_mix(h, static_cast<uint32_t>(p.chord.type));
    h = fp_mix(h, static_cast<uint32_t>(p.transpose.semitones));
    h = fp_mix(h, static_cast<uint32_t>(p.transpose.octaves));
    h = fp_mix(h, p.timing.bpm);
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
        cancel_pending(current_arp_note_, true);
        current_arp_note_ = kCapNote;
    }
    arp_active_ = false;
    arp_seq_count_ = 0;

    if (merged.count == 0) {
        if (current_arp_note_ != kCapNote && midi_) {
            midi_->note_off(current_arp_note_);
            cancel_pending(current_arp_note_, true);
            current_arp_note_ = kCapNote;
        }
        return;
    }

    const Pattern& p = state_->active_pattern();
    build_arp_sequence(merged, p.arp, p.key_filter, arp_seq_, arp_seq_count_);
    if (arp_seq_count_ == 0) {
        if (current_arp_note_ != kCapNote && midi_) {
            midi_->note_off(current_arp_note_);
            cancel_pending(current_arp_note_, true);
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
    // While the pattern editor is on screen ALL note/pattern generation is
    // blocked: a note key only ASSIGNS the pressed pitch to the step the
    // cursor/playhead currently sits on. The marker stays on that step so the
    // +/- direction of the just-written note is visible at once; the step is
    // advanced by Play (or by moving the cursor). This is routed first so a
    // RandomNote/RandomPattern loop cannot regenerate while the user edits.
    if (state_->runtime.screen_mode == ScreenMode::Edit) {
        auto& ed = state_->editor;
        Pattern& pat = state_->active_pattern();
        const uint8_t idx = std::min<uint8_t>(
            static_cast<uint8_t>(std::min<int>(state_->runtime.current_step,
                                               kStepCountMax - 1)),
            kStepCountMax - 1);
        Step& s = pat.steps[idx];
        // Remember the step's ORIGINAL note the first time it is edited, as a
        // reference to draw the +/- direction and to restore (Rest = undo).
        if (ed.prev_notes[idx] < 0) {
            ed.prev_notes[idx] = static_cast<int16_t>(s.notes[0]);
        }
        ed.prev_note = ed.prev_notes[idx];
        ed.cur = idx;
        ed.selected = idx;
        ed.has_selected = true;
        s.notes[0] = raw_note;
        s.note_count = 1;
        s.active = true;
        cap_pending_[chip_idx] = true;
        cap_note_[chip_idx] = raw_note;
        cap_step_[chip_idx] = idx;
        cap_start_ms_[chip_idx] = now_ms;
        ui_repaint_ = true;
        return;
    }

    const PlayMode mode = state_->runtime.mode;
    if (mode == PlayMode::Pattern) {
        // Recording mode: capture the pressed key (post-filter) at the grid
        // position and keep monitoring it live until release.
        bool muted = false;
        const uint8_t snapped =
            key_filter_apply(raw_note, state_->active_pattern().key_filter, muted);
        pattern_capture(snapped, now_ms);
        cap_pending_[chip_idx] = true;
        cap_note_[chip_idx] = snapped;
        cap_step_[chip_idx] = static_cast<uint8_t>(
            state_->runtime.current_step);
        cap_start_ms_[chip_idx] = now_ms;
        if (midi_ != nullptr && !state_->runtime.live_mute) {
            midi_->note_on(snapped, 100);  // live monitor
        }
        return;
    }

    const bool latch = latch_enabled();
    const bool arp = arp_enabled();


    // RandomNote: each pressed key raises the anchor octave of the continuous
    // random generator. The first press (or Play) starts the loop; it keeps
    // emitting random notes until RandomNote is left or Play is pressed again.
    // Re-pressing the same key that just produced the current tone toggles the
    // generation back off (and clears the readout).
    if (mode == PlayMode::RandomNote) {
        if (latch) {
            latched_[chip_idx] = true;
        }
        if (random_loop_ && state_ != nullptr &&
            state_->runtime.last_input_note == raw_note) {
            random_loop_stop();
            state_->runtime.last_input_note = 0;
            state_->runtime.last_note = 0;
            state_->runtime.show_note = false;
            ui_repaint_ = true;
            return;
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

    // RandomPattern (GEN): a key press regenerates the slot from this anchor
    // and starts the loop; re-pressing the SAME key toggles generation off.
    if (mode == PlayMode::RandomPattern) {
        if (latch) {
            latched_[chip_idx] = true;
        }
        if (gen_playing_ && state_ != nullptr &&
            state_->runtime.last_input_note == raw_note) {
            gen_stop();
            state_->runtime.playing = false;
            state_->runtime.last_input_note = 0;
            state_->runtime.last_note = 0;
            state_->runtime.show_note = false;
            ui_repaint_ = true;
            return;
        }
        if (state_ != nullptr) {
            state_->runtime.last_input_note = raw_note;
            state_->runtime.show_note = true;
        }
        gen_start(raw_note, now_ms);
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
            release_chip(chip_idx, now_ms);
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
            release_chip(chip_idx, now_ms);
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

    // Non-arp live: spread the chord per voicing (Block immediate, Strum
    // staggered, Roll staggered with short plucked tails) over the gate attack.
    const Pattern& p = state_->active_pattern();
    const NoteSet set = build_note_set(raw_note, p);
    if (set.count == 0) {
        return;
    }
    if (state_ != nullptr) {
        state_->runtime.last_note = set.notes[0];
        state_->runtime.show_note = true;
        // Live-overdub: while Rec is armed the raw played key is written into
        // the active slot at the quantised grid position — an empty step gets
        // the note, an already-filled step is REPLACED by this new one.
        if (state_->runtime.recording && state_->runtime.mode != PlayMode::Pattern) {
            pattern_capture(raw_note, now_ms);
        }
    }
    const uint32_t attack = gate_attack_ms();
    if (p.chord.voicing == VoicingMode::Block) {
        for (uint8_t i = 0; i < set.count; ++i) {
            schedule_pending(now_ms + attack, set.notes[i], true);
        }
    } else {
        const uint32_t stride = (set.count > 1) ? p.chord.strum_delay_ms : 0;
        if (p.chord.voicing == VoicingMode::Strum) {
            for (uint8_t i = 0; i < set.count; ++i) {
                schedule_pending(now_ms + attack + i * stride, set.notes[i], true);
            }
        } else {  // Roll: pluck each note until the last, which sustains.
            for (uint8_t i = 0; i < set.count; ++i) {
                schedule_pending(now_ms + attack + i * stride, set.notes[i], true);
                if (i + 1 < set.count) {
                    schedule_pending(now_ms + attack + (i + 1) * stride, set.notes[i], false);
                }
            }
        }
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
    if (state_->runtime.screen_mode == ScreenMode::Edit) {
        // Rec+Play: the duration of a just-written note is measured from how
        // long the key was held, so one long press can fill a whole page. With
        // Rec off none of this sustains a sounding note; just clear the pending
        // capture slot so nothing lingers for the next press.
        if (cap_pending_[chip_idx]) {
            const bool fixdur =
                state_->runtime.recording && state_->runtime.playing;
            cap_pending_[chip_idx] = false;
            const uint8_t cap = cap_note_[chip_idx];
            if (fixdur && cap != 0) {
                const uint32_t held = now_ms - cap_start_ms_[chip_idx];
                const uint8_t div = nearest_len_div(
                    held, state_->active_pattern().timing.bpm);
                const uint8_t idx = cap_step_[chip_idx];
                if (idx < kStepCountMax) {
                    auto& s = state_->active_pattern().steps[idx];
                    if (s.notes[0] == cap) {
                        s.len_div = div;
                    }
                }
                if (midi_ != nullptr) {
                    midi_->note_off(cap);
                }
            }
            cap_note_[chip_idx] = 0;
        }
        return;
    }
    if (state_->runtime.mode == PlayMode::Pattern) {
        // End the monitor note and fix the recorded step's duration from the
        // measured hold time.
        if (cap_pending_[chip_idx]) {
            cap_pending_[chip_idx] = false;
            const uint32_t held = now_ms - cap_start_ms_[chip_idx];
            const uint8_t div = nearest_len_div(held, state_->active_pattern().timing.bpm);
            const uint8_t idx = cap_step_[chip_idx];
            if (idx < kStepCountMax) {
                auto& s = state_->active_pattern().steps[idx];
                if (s.notes[0] == cap_note_[chip_idx]) {
                    s.len_div = div;
                }
            }
            if (midi_ != nullptr && cap_note_[chip_idx] != 0) {
                midi_->note_off(cap_note_[chip_idx]);
            }
            cap_note_[chip_idx] = 0;
        }
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
    release_chip(chip_idx, now_ms);
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
                    release_chip(c, now_ms);
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
    process_pending(now_ms);
    check_config(now_ms);
    // While the pattern editor is on screen, suppress generator advancement:
    // a running RandomNote/RandomPattern loop must not keep rewriting the slot
    // the user is editing. PTRN audition playback (started with Play) keeps
    // running so the loop below advances the playhead.
    if (state_ != nullptr && state_->runtime.screen_mode == ScreenMode::Edit) {
        if (random_loop_) {
            random_loop_stop();
        }
        if (gen_playing_) {
            gen_stop();
        }
    }
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
    // RandomPattern (GEN): duration-chained playback of the generated slot.
    if (gen_playing_ && state_ != nullptr &&
        state_->runtime.mode == PlayMode::RandomPattern) {
        gen_advance(now_ms);
    } else if (gen_playing_) {
        gen_stop();
    }
    // Pattern transport: grid playback in PTRN mode, silent grid while
    // recording RND output elsewhere, and full audition grid while the
    // pattern editor is on screen (Play there always runs the slot — the
    // advancing playhead is what the user hears, whatever play mode they
    // originally came from).
    if (ptn_playing_ && state_ != nullptr) {
        if (state_->runtime.screen_mode == ScreenMode::Edit ||
            state_->runtime.mode == PlayMode::RandomPattern || capture_only_) {
            pattern_advance(now_ms);
        } else {
            capture_transport_stop();
        }
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
        cancel_pending(last_random_note_, true);
    }
    last_random_note_ = 0;
    next_random_ms_ = 0;
    ui_repaint_ = true;
}

void ModeEngine::random_loop_start(uint8_t anchor, uint32_t now_ms) {
    random_loop_ = true;
    random_anchor_ = anchor;
    // First note goes out immediately; the rest follow every arp period.
    next_random_ms_ = now_ms;
    (void)advance_random(now_ms);
}

// -- RandomPattern (GEN): generate once into the slot, loop as a chain -------

void ModeEngine::gen_regenerate(uint8_t anchor) {
    if (state_ == nullptr) {
        return;
    }
    gen_anchor_ = anchor;
    Pattern& p = state_->active_pattern();
    // Generation overwrites the whole slot: stale notes beyond the loop
    // length must not survive into the new pattern.
    for (auto& s : p.steps) {
        s.notes[0] = 0;
        s.note_count = 0;
        s.active = false;
        s.tie = false;
        s.len_div = 8;
    }
    for (auto& pn : state_->editor.prev_notes) {
        pn = -1;  // a fresh pattern has no per-step "original" edits
    }
    state_->editor.prev_note = -1;
    const RandomCfg& rnd = p.random;
    const bool tr = rnd.len_triplets;
    int vis_lo = note_len_div_pos(rnd.len_min_idx, tr);
    int vis_hi = note_len_div_pos(rnd.len_max_idx, tr);
    if (vis_lo > vis_hi) {
        const int t = vis_lo;
        vis_lo = vis_hi;
        vis_hi = t;
    }
    const int vis_max = note_len_div_count(tr) - 1;
    if (vis_hi > vis_max) { vis_hi = vis_max; }
    if (vis_lo > vis_hi) { vis_lo = vis_hi; }

    const uint16_t rep_pct = static_cast<uint16_t>(rnd.repeat) * 10u;
    const uint8_t events =
        static_cast<uint8_t>(std::min<int>(p.length, kStepCountMax));
    uint8_t prev_note = 0;
    for (uint8_t i = 0; i < events; ++i) {
        Step& s = p.steps[i];
        const bool rest = rng_.range(100) < rnd.density_or_probability;
        uint8_t note = 0;
        if (!rest) {
            if (prev_note != 0 && rep_pct > 0 && rng_.range(100) < rep_pct) {
                note = prev_note;  // REP: repeat the previous event's tone
            } else {
                note = pick_random_note(anchor);
            }
            prev_note = note;
        }
        s.notes[0] = note;
        s.note_count = rest ? 0 : 1;
        s.active = !rest;
        s.tie = false;
        const int vis =
            vis_lo + static_cast<int>(rng_.range(static_cast<uint32_t>(vis_hi - vis_lo + 1)));
        s.len_div = note_len_div_real(vis, tr);  // store the division index
    }
}

void ModeEngine::gen_start(uint8_t anchor_note, uint32_t now_ms) {
    if (state_ == nullptr || midi_ == nullptr) {
        return;
    }
    gen_stop();
    random_loop_stop();
    gen_regenerate(anchor_note);
    gen_playing_ = true;
    gen_pos_ = 0;
    gen_last_note_ = 0;
    gen_next_ms_ = now_ms;
    state_->runtime.playing = true;
    state_->runtime.current_step = 0;
    ui_repaint_ = true;
}

void ModeEngine::gen_toggle_play() {
    if (state_ == nullptr) {
        return;
    }
    if (gen_playing_) {
        gen_stop();
        state_->runtime.playing = false;
    } else {
        gen_playing_ = true;
        gen_pos_ = 0;
        gen_last_note_ = 0;
        gen_next_ms_ = 0;  // fire on the next tick
        state_->runtime.playing = true;
        state_->runtime.current_step = 0;
        ui_repaint_ = true;
    }
}

void ModeEngine::gen_stop() {
    if (!gen_playing_) {
        return;
    }
    gen_playing_ = false;
    clear_pending();
    if (gen_last_note_ != 0 && midi_) {
        midi_->note_off(gen_last_note_);
    }
    gen_last_note_ = 0;
    ui_repaint_ = true;
}

void ModeEngine::gen_regen_now(uint32_t now_ms) {
    if (!gen_playing_ || state_ == nullptr) {
        return;
    }
    gen_regenerate(gen_anchor_);
    gen_pos_ = 0;
    gen_last_note_ = 0;
    clear_pending();
    gen_next_ms_ = now_ms;
    state_->runtime.current_step = 0;
    ui_repaint_ = true;
}

void ModeEngine::gen_advance(uint32_t now_ms) {
    if (midi_ == nullptr || state_ == nullptr) {
        return;
    }
    if (now_ms < gen_next_ms_) {
        return;
    }
    Pattern& p = state_->active_pattern();
    const uint8_t events =
        static_cast<uint8_t>(std::min<int>(p.length, kStepCountMax));
    if (events == 0) {
        gen_stop();
        return;
    }
    Step& s = p.steps[gen_pos_];
    const uint32_t dur = note_len_ms(s.len_div, p.timing.bpm);
    const uint32_t attack = gate_attack_ms();
    const uint32_t release = gate_release_ms();
    // Gate as a share of the event length (Gate %): 100 keeps the legato
    // chain, lower values leave an articulation gap before the next onset.
    uint32_t gate = dur * p.random.gate_pct / 100u;
    if (gate < 1) { gate = 1; }

    if (s.active && s.note_count > 0 && s.notes[0] != 0) {
        schedule_pending(now_ms + attack, s.notes[0], true);
        schedule_pending(now_ms + gate + release, s.notes[0], false);
        gen_last_note_ = s.notes[0];
        state_->runtime.last_note = s.notes[0];
        state_->runtime.show_note = true;
    } else {
        state_->runtime.show_note = false;
    }
    state_->runtime.current_step = gen_pos_;
    gen_pos_ = static_cast<uint8_t>((gen_pos_ + 1) % events);
    gen_next_ms_ = now_ms + dur;
    ui_repaint_ = true;
}

// -- Pattern (PTRN slot) transport + recording capture ------------------------

uint32_t ModeEngine::pattern_step_ms() const {
    const Pattern& p = state_->active_pattern();
    float quarter = 60000.0f;
    float bpm_f = p.timing.bpm;
    if (bpm_f < 20.0f) { bpm_f = 120.0f; }
    quarter /= bpm_f;
    // Grid step: 1/16 note by default, 1/64 with Pattern.grid64.
    const float step = p.grid64 ? quarter / 16.0f : quarter / 4.0f;
    return static_cast<uint32_t>(step < 1.0f ? 1.0f : step);
}

void ModeEngine::pattern_start(uint32_t now_ms) {
    ptn_playing_ = true;
    ptn_pos_ = 0;
    ptn_last_note_ = 0;
    ptn_anchor_ms_ = now_ms;
    ptn_next_ms_ = now_ms;
    state_->runtime.current_step = 0;
}

void ModeEngine::pattern_stop() {
    if (!ptn_playing_) {
        return;
    }
    ptn_playing_ = false;
    clear_pending();
    if (ptn_last_note_ != 0 && midi_) {
        midi_->note_off(ptn_last_note_);
    }
    ptn_last_note_ = 0;
    for (auto& c : cap_pending_) {
        c = false;
    }
    ui_repaint_ = true;
}

void ModeEngine::pattern_toggle(uint32_t now_ms) {
    if (ptn_playing_) {
        pattern_stop();
        state_->runtime.playing = false;
    } else {
        pattern_start(now_ms);
        state_->runtime.playing = true;
        ui_repaint_ = true;
    }
}

void ModeEngine::capture_transport_start(uint32_t now_ms) {
    // Silent grid transport used to quantise RND output while recording.
    if (!ptn_playing_) {
        ptn_playing_ = true;      // timing only; notes fire only in PTRN mode
        capture_only_ = true;
        ptn_pos_ = 0;
        ptn_anchor_ms_ = now_ms;
        ptn_next_ms_ = now_ms;
        state_->runtime.current_step = 0;
    }
    // Recording does NOT wipe the slot: only the notes the player actually
    // produces while Rec is armed are written (new steps get the note, filled
    // steps are replaced). Steps nobody touches are left as they were.
    ui_repaint_ = true;
}

void ModeEngine::capture_transport_stop() {
    if (capture_only_) {
        capture_only_ = false;
        ptn_playing_ = false;
        ptn_pos_ = 0;
    }
}

// Nearest kNoteLenDivs index for a measured duration in ms.
uint8_t nearest_len_div(uint32_t dur_ms, uint16_t bpm) {
    uint8_t best = 8;
    uint32_t best_dist = 0xFFFFFFFFu;
    for (uint8_t i = 0; i < kNoteLenDivCount; ++i) {
        const uint32_t d = note_len_ms(i, bpm);
        const uint32_t dist = (d > dur_ms) ? d - dur_ms : dur_ms - d;
        if (dist < best_dist) {
            best_dist = dist;
            best = i;
        }
    }
    return best;
}

int16_t ModeEngine::pattern_capture(uint8_t note, uint32_t now_ms, int8_t force_div) {
    if (state_ == nullptr || !state_->runtime.recording || !ptn_playing_) {
        return -1;
    }
    Pattern& p = state_->active_pattern();
    const uint32_t step_ms = pattern_step_ms();
    const uint32_t rel = now_ms - ptn_anchor_ms_;
    // Round to the nearest grid position and wrap over the loop length.
    uint32_t idx = (rel + step_ms / 2u) / step_ms;
    const int len = std::max<int>(1, std::min<int>(p.length, kStepCountMax));
    idx %= static_cast<uint32_t>(len);
    Step& s = p.steps[idx];
    s.notes[0] = note;
    s.note_count = 1;
    s.active = true;
    s.tie = false;
    if (force_div >= 0) {
        s.len_div = static_cast<uint8_t>(force_div);
    }
    state_->runtime.current_step = static_cast<uint8_t>(idx);
    ui_repaint_ = true;
    return static_cast<int16_t>(idx);
}

void ModeEngine::capture_rnd_note(uint8_t note, uint8_t div, uint32_t now_ms) {
    if (capture_only_) {
        (void)pattern_capture(note, now_ms, static_cast<int8_t>(div));
    }
}

void ModeEngine::pattern_advance(uint32_t now_ms) {
    if (midi_ == nullptr || state_ == nullptr) {
        return;
    }
    if (now_ms < ptn_next_ms_) {
        return;
    }
    const bool fire = !capture_only_;
    Pattern& p = state_->active_pattern();
    const uint8_t events =
        static_cast<uint8_t>(std::max<int>(1, std::min<int>(p.length, kStepCountMax)));
    const uint32_t step_ms = pattern_step_ms();
    // Swing pushes every odd boundary later by pct of one step.
    const bool odd = (ptn_pos_ & 1u) != 0u;
    uint32_t swing = odd
        ? static_cast<uint32_t>(step_ms * p.timing.swing_pct / 100u)
        : 0u;

    if (fire) {
        Step& s = p.steps[ptn_pos_];
        const uint32_t attack = gate_attack_ms();
        const uint32_t release = gate_release_ms();
        if (ptn_last_note_ != 0) {
            schedule_pending(now_ms + swing + release, ptn_last_note_, false);
            ptn_last_note_ = 0;
        }
        if (s.active && s.note_count > 0 && s.notes[0] != 0) {
            schedule_pending(now_ms + swing + attack, s.notes[0], true);
            uint32_t gate = note_len_ms(s.len_div, p.timing.bpm)
                            * p.random.gate_pct / 100u;
            if (gate < 1) { gate = 1; }
            schedule_pending(now_ms + swing + gate + release, s.notes[0], false);
            ptn_last_note_ = s.notes[0];
            state_->runtime.last_note = s.notes[0];
            state_->runtime.show_note = true;
        } else {
            state_->runtime.show_note = false;
        }
    } else if (odd) {
        swing = 0;
    }

    state_->runtime.current_step = ptn_pos_;
    ptn_pos_ = static_cast<uint8_t>((ptn_pos_ + 1) % events);
    ptn_next_ms_ = now_ms + (step_ms - swing > 0 ? step_ms - swing : 1u);
    ui_repaint_ = true;
}

uint8_t ModeEngine::anchor_in_range() const {
    const Pattern& p = state_->active_pattern();
    const int lo = std::min(static_cast<int>(p.random.note_min),
                            static_cast<int>(p.random.note_max));
    const int hi = std::max(static_cast<int>(p.random.note_min),
                            static_cast<int>(p.random.note_max));
    const int pc = random_anchor_ % 12;
    // Nearest in-range occurrence of the anchor's pitch class. A span narrower
    // than 12 semitones may not contain it at all — fall back to clamping.
    int best = -1;
    int best_dist = 1 << 30;
    for (int n = lo; n <= hi; ++n) {
        if (n % 12 != pc) {
            continue;
        }
        const int dist = (n > random_anchor_) ? n - random_anchor_
                                              : random_anchor_ - n;
        if (dist < best_dist) {
            best_dist = dist;
            best = n;
        }
    }
    if (best >= 0) {
        return static_cast<uint8_t>(best);
    }
    return static_cast<uint8_t>(std::clamp<int>(random_anchor_, lo, hi));
}


void ModeEngine::advance_random(uint32_t now_ms) {
    if (!random_loop_ || midi_ == nullptr || state_ == nullptr) {
        return;
    }
    if (now_ms < next_random_ms_) {
        return;
    }
    const Pattern& p = state_->active_pattern();
    const RandomCfg& rnd = p.random;
    const uint32_t interval = arp_interval_ms(p.arp, p.timing.bpm);
    const uint32_t attack = gate_attack_ms();
    const uint32_t release = gate_release_ms();
    uint32_t boundary = 0;

    // Repeatability: with probability repeat*10 % echo the anchor note (the
    // key that started the run, or the latest pressed key) instead of drawing
    // a fresh tone. The anchor goes out as-is: it is the player's explicit
    // input, so range and scale filters do not clip it.
    uint8_t picked;
    bool rep_hit = false;  // this step echoes the KEY note (the anchor)
    const uint16_t rep_pct = static_cast<uint16_t>(rnd.repeat) * 10u;
    if (rep_pct > 0 && rng_.range(100) < rep_pct) {
        // The KEY note is the main one: keep its pitch class, snap the octave
        // into the PITCH range when the raw anchor lies outside it.
        picked = anchor_in_range();
        rep_hit = true;
    } else {
        picked = pick_random_note(random_anchor_);
    }

    // Draw the gate length inside the configured LEN span (visible positions
    // honour the triplet filter; storage keeps real table indices).
    uint8_t drawn_div = 8;
    {
        const bool tr = rnd.len_triplets;
        int vis_lo = note_len_div_pos(rnd.len_min_idx, tr);
        int vis_hi = note_len_div_pos(rnd.len_max_idx, tr);
        if (vis_lo > vis_hi) { const int t = vis_lo; vis_lo = vis_hi; vis_hi = t; }
        const int vis_max = note_len_div_count(tr) - 1;
        if (vis_hi > vis_max) { vis_hi = vis_max; }
        if (vis_lo > vis_hi) { vis_lo = vis_hi; }
        const int vis =
            vis_lo + static_cast<int>(rng_.range(static_cast<uint32_t>(vis_hi - vis_lo + 1)));
        drawn_div = note_len_div_real(vis, tr);
        boundary = note_len_ms(drawn_div, p.timing.bpm);
    }

    if (!rnd.len_chain) {
        // Grid mode (Len Chain Off): steps stay on the ARP Rate grid and the
        // drawn length is capped by the step. Identical redraws ring through —
        // EXCEPT a REP hit: the KEY note must be re-articulated, otherwise the
        // repeat would be inaudible while the anchor keeps sounding.
        if (picked == last_random_note_ && last_random_note_ != 0 && !rep_hit) {
            next_random_ms_ = now_ms + interval;
            return;
        }
        if (last_random_note_ != 0 && midi_) {
            schedule_pending(now_ms + release, last_random_note_, false);
        }
        last_random_note_ = picked;
        if (midi_) {
            schedule_pending(now_ms + attack, picked, true);
            if (boundary > interval) { boundary = interval; }
            schedule_pending(now_ms + boundary + release, picked, false);
        }
        capture_rnd_note(picked, drawn_div, now_ms);
        if (state_ != nullptr) {
            state_->runtime.last_note = picked;
            state_->runtime.show_note = true;
        }
        ui_repaint_ = true;
        next_random_ms_ = now_ms + interval;
        return;
    }

    // Chained mode (Len Chain On): every step is an articulated event — on,
    // then a timed off at gate% of the drawn length — and the next onset lands
    // exactly on this length's end, so a "64" note is followed immediately by
    // whatever length comes next. The ARP Rate does not space RND notes here
    // (BPM still scales divisions). Identical tones retrigger too: REP repeats
    // become rhythmic pulses.
    if (boundary < 20) { boundary = 20; }  // USB MIDI sanity floor
    uint32_t gate = boundary * rnd.gate_pct / 100u;
    if (gate < 1) { gate = 1; }
    last_random_note_ = picked;
    if (midi_) {
        schedule_pending(now_ms + attack, picked, true);
        schedule_pending(now_ms + gate + release, picked, false);
    }
    capture_rnd_note(picked, drawn_div, now_ms);
    if (state_ != nullptr) {
        state_->runtime.last_note = picked;
        state_->runtime.show_note = true;
    }
    ui_repaint_ = true;
    next_random_ms_ = now_ms + boundary;
}

uint8_t ModeEngine::pick_random_note(uint8_t anchor) {
    const Pattern& p = state_->active_pattern();
    const RandomCfg& rnd = p.random;
    const KeyFilterCfg& kf = p.key_filter;
    const bool use_kf = kf.enabled && kf.scale != ScaleId::Off &&
                        static_cast<uint8_t>(kf.scale) < static_cast<uint8_t>(ScaleId::kCount);
    // Draw inside the user-configured random range, clamped to a sane band
    // that stays within MIDI. When the range is empty or inverted, fall back
    // to the anchor-based band so the mode always produces a note.
    const int lo = std::min(static_cast<int>(rnd.note_min), static_cast<int>(rnd.note_max));
    const int hi = std::max(static_cast<int>(rnd.note_min), static_cast<int>(rnd.note_max));
    const bool usable = hi >= lo;
    // A 3.5-octave band around the pressed key: center sits one octave below
    // the anchor so the generated note mostly stays in a pleasant range.
    const int center = static_cast<int>(anchor) - 12;
    for (int attempt = 0; attempt < 16; ++attempt) {
        int midi;
        if (usable) {
            midi = lo + static_cast<int>(rng_.range(static_cast<uint32_t>(hi - lo + 1)));
        } else {
            midi = center + static_cast<int>(rng_.range(48));
        }
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

// -- Delayed note events (gate attack/release, voicing strum, timing) -------

uint32_t ModeEngine::gate_attack_ms() const {
    if (state_ == nullptr || !state_->active_pattern().gate.enabled) {
        return 0;
    }
    return state_->active_pattern().gate.attack_ms;
}

uint32_t ModeEngine::gate_release_ms() const {
    if (state_ != nullptr && state_->active_pattern().gate.enabled) {
        return state_->active_pattern().gate.release_ms;
    }
    return 0;
}

void ModeEngine::clear_pending() {
    for (uint8_t i = 0; i < kMaxPendingEvents; ++i) {
        pending_[i].active = false;
    }
}

void ModeEngine::cancel_pending(uint8_t note, bool on) {
    for (uint8_t i = 0; i < kMaxPendingEvents; ++i) {
        if (pending_[i].active && pending_[i].on == on && pending_[i].note == note) {
            pending_[i].active = false;
        }
    }
}

void ModeEngine::schedule_pending(uint32_t at_ms, uint8_t note, bool on) {
    // Reuse an expired/free slot; if the ring is still full, fire immediately
    // (deterministic fallback rather than silently dropping the event).
    for (uint8_t i = 0; i < kMaxPendingEvents; ++i) {
        const uint8_t idx = static_cast<uint8_t>((pending_cursor_ + i) % kMaxPendingEvents);
        if (!pending_[idx].active) {
            pending_[idx].at_ms = at_ms;
            pending_[idx].note = note;
            pending_[idx].on = on;
            pending_[idx].active = true;
            pending_cursor_ = static_cast<uint8_t>((idx + 1) % kMaxPendingEvents);
            return;
        }
    }
    if (on && midi_ != nullptr) {
        midi_->note_on(note, 100);
    } else if (midi_ != nullptr) {
        midi_->note_off(note);
    }
}

void ModeEngine::process_pending(uint32_t now_ms) {
    for (uint8_t i = 0; i < kMaxPendingEvents; ++i) {
        const PendingEvent& e = pending_[i];
        if (e.active && now_ms >= e.at_ms) {
            if (e.on) {
                if (midi_ != nullptr) {
                    midi_->note_on(e.note, 100);
                }
            } else if (midi_ != nullptr) {
                midi_->note_off(e.note);
            }
            pending_[i].active = false;
        }
    }
}

uint32_t ModeEngine::next_arp_onset(const Pattern& p, uint32_t now_ms, uint32_t interval) {
    uint32_t base = now_ms + interval;
    // Swing: delay the off-beat (2nd) step of each pair by a fraction of the
    // step duration. arp_index_ is the 1-based ordinal of the fired step.
    if (p.timing.swing_pct > 0 && (arp_index_ & 1U) == 1U) {
        base += (static_cast<uint32_t>(interval) * p.timing.swing_pct) / 100U;
    }
    // Humanize: nudge each step forward by a deterministic 0..N ms jitter.
    if (p.timing.humanize_ms > 0) {
        base += rng_.range(p.timing.humanize_ms);
    }
    // Quantize: snap the onset up to the next grid boundary.
    const uint32_t grid = quantize_grid_ms(p.timing.quantize_grid, p.timing.bpm);
    if (grid > 0) {
        base = ((base + grid - 1u) / grid) * grid;
    }
    return base;
}

}  // namespace drom