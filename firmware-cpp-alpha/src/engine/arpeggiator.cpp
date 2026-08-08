#include "arpeggiator.hpp"

#include <algorithm>

namespace drom {

// ---------------------------------------------------------------------------
// SimpleRng (xorshift32)
// ---------------------------------------------------------------------------

SimpleRng::SimpleRng(uint32_t seed) { this->seed(seed); }

void SimpleRng::seed(uint32_t s) {
    if (s == 0) { s = 0x9E3779B9u; }
    state_ = s;
}

uint32_t SimpleRng::next() {
    uint32_t x = state_;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    state_ = x;
    return x;
}

uint32_t SimpleRng::range(uint32_t max) {
    if (max == 0) { return 0; }
    return next() % max;
}

int32_t SimpleRng::range_int(int32_t lo, int32_t hi) {
    if (hi <= lo) { return lo; }
    return lo + static_cast<int32_t>(range(static_cast<uint32_t>(hi - lo + 1)));
}

// ---------------------------------------------------------------------------
// Arpeggio range + style generation (port of arpeggiator.py)
// ---------------------------------------------------------------------------

namespace {

void append_range(NoteSet& out, const NoteSet& base, uint8_t range_semitones) {
    if (range_semitones == 0) {
        for (uint8_t i = 0; i < base.count; ++i) {
            if (out.count < out.notes.size()) { out.notes[out.count++] = base.notes[i]; }
        }
        return;
    }
    uint8_t min_note = base.notes[0];
    for (uint8_t i = 1; i < base.count; ++i) {
        if (base.notes[i] < min_note) { min_note = base.notes[i]; }
    }
    for (uint8_t octave = 0; octave < static_cast<uint8_t>(range_semitones / 12) + 2; ++octave) {
        for (uint8_t i = 0; i < base.count; ++i) {
            const uint16_t shifted = static_cast<uint16_t>(base.notes[i]) + static_cast<uint16_t>(octave) * 12;
            if (shifted <= 127 && (shifted - min_note) <= range_semitones) {
                if (out.count < out.notes.size()) {
                    out.notes[out.count++] = static_cast<uint8_t>(shifted);
                }
            }
        }
    }
}

}  // namespace

void build_arp_sequence(const NoteSet& base, const ArpCfg& cfg,
                        std::array<uint8_t, kMaxArpNotes>& out, uint8_t& out_count) {
    out_count = 0;
    if (base.count == 0) {
        return;
    }

    NoteSet extended {};
    append_range(extended, base, cfg.range_semitones);
    if (extended.count == 0) {
        extended.notes[0] = base.notes[0];
        extended.count = 1;
    }

    const uint8_t n = extended.count;
    const uint8_t steps = cfg.num_steps > 0 ? cfg.num_steps : 1;
    if (n == 1) {
        for (uint8_t i = 0; i < steps && out_count < out.size(); ++i) {
            out[out_count++] = extended.notes[0];
        }
        return;
    }

    const auto push = [&](uint8_t note) {
        if (out_count < out.size()) { out[out_count++] = note; }
    };

    switch (cfg.style) {
        case ArpStyle::Up:
        case ArpStyle::AsPlayed:
            for (uint8_t i = 0; i < steps; ++i) { push(extended.notes[i % n]); }
            break;
        case ArpStyle::Down:
            for (uint8_t i = 0; i < steps; ++i) { push(extended.notes[n - (i % n) - 1]); }
            break;
        case ArpStyle::UpDown: {
            std::array<uint8_t, kMaxArpNotes * 2> pattern {};
            uint16_t p = 0;
            for (uint8_t i = 0; i < n; ++i) { pattern[p++] = extended.notes[i]; }
            for (int16_t i = static_cast<int16_t>(n) - 2; i > 0; --i) { pattern[p++] = extended.notes[static_cast<uint8_t>(i)]; }
            for (uint8_t i = 0; i < steps; ++i) { push(pattern[i % p]); }
            break;
        }
        case ArpStyle::DownUp: {
            std::array<uint8_t, kMaxArpNotes * 2> pattern {};
            uint16_t p = 0;
            for (uint8_t i = 0; i < n; ++i) { pattern[p++] = extended.notes[n - i - 1]; }
            for (int16_t i = static_cast<int16_t>(n) - 2; i > 0; --i) { pattern[p++] = extended.notes[static_cast<uint8_t>(i)]; }
            for (uint8_t i = 0; i < steps; ++i) { push(pattern[i % p]); }
            break;
        }
        case ArpStyle::Random: {
            SimpleRng rng(0xAC010203u + base.notes[0]);
            for (uint8_t i = 0; i < steps; ++i) { push(extended.notes[rng.range(n)]); }
            break;
        }
        case ArpStyle::ConvergeDiverge: {
            std::array<uint8_t, kMaxArpNotes * 2> pattern {};
            uint16_t p = 0;
            uint8_t left = 0;
            uint8_t right = n - 1;
            while (left <= right) {
                pattern[p++] = extended.notes[left];
                if (left != right) { pattern[p++] = extended.notes[right]; }
                ++left;
                --right;
            }
            const uint16_t diverge_len = p;
            uint16_t full_len = p;
            if (diverge_len > 2) {
                for (uint16_t i = 1; i + 1 < diverge_len; ++i) {
                    pattern[full_len++] = pattern[diverge_len - 1 - i];
                }
            }
            for (uint8_t i = 0; i < steps; ++i) { push(pattern[i % full_len]); }
            break;
        }
        case ArpStyle::Off:
        default:
            for (uint8_t i = 0; i < steps; ++i) { push(extended.notes[i % n]); }
            break;
    }
}

void Arpeggiator::reset() {
    count_ = 0;
    index_ = 0;
}

void Arpeggiator::rebuild(const NoteSet& input, const ArpCfg& cfg) {
    reset();
    build_arp_sequence(input, cfg, sequence_, count_);
}

bool Arpeggiator::active() const {
    return count_ > 0;
}

uint8_t Arpeggiator::next_note() {
    if (count_ == 0) {
        return 0;
    }
    const uint8_t note = sequence_[index_ % count_];
    index_ = static_cast<uint8_t>((index_ + 1) % count_);
    return note;
}

}  // namespace drom
