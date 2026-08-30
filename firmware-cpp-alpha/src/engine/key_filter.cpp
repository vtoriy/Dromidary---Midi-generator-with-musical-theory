#include "key_filter.hpp"

#include <algorithm>

namespace drom {

namespace {

// Interval patterns (in semitones) for every scale, mirroring key_filter.py.
// Indexed by ScaleId (Off has no intervals). Declared for the widest row
// (Chromatic has 12 entries); shorter rows are zero-padded.
constexpr uint8_t kScaleIntervals[][12] = {
    /* Off                */ {0},
    /* Major              */ {0, 2, 4, 5, 7, 9, 11},
    /* Minor              */ {0, 2, 3, 5, 7, 8, 10},
    /* Dorian             */ {0, 2, 3, 5, 7, 9, 10},
    /* Phrygian           */ {0, 1, 3, 5, 7, 8, 10},
    /* Lydian             */ {0, 2, 4, 6, 7, 9, 11},
    /* Mixolydian         */ {0, 2, 4, 5, 7, 9, 10},
    /* Locrian            */ {0, 1, 3, 5, 6, 8, 10},
    /* HarmonicMinor      */ {0, 2, 3, 5, 7, 8, 11},
    /* MelodicMinor       */ {0, 2, 3, 5, 7, 9, 11},
    /* PentatonicMajor    */ {0, 2, 4, 7, 9},
    /* PentatonicMinor    */ {0, 3, 5, 7, 10},
    /* Blues              */ {0, 3, 5, 6, 7, 10},
    /* WholeTone          */ {0, 2, 4, 6, 8, 10},
    /* Diminished         */ {0, 2, 3, 5, 6, 8, 9, 11},
    /* Chromatic          */ {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11},
};

constexpr uint8_t kScaleIntervalCount[16] = {
    0, 7, 7, 7, 7, 7, 7, 7, 7, 7, 5, 5, 6, 6, 8, 12,
};

}  // namespace

bool note_in_scale(uint8_t note, const KeyFilterCfg& cfg) {
    if (!cfg.enabled || cfg.scale == ScaleId::Off) {
        return true;
    }
    const uint8_t id = static_cast<uint8_t>(cfg.scale);
    if (id >= static_cast<uint8_t>(ScaleId::kCount)) {
        return true;
    }
    const uint8_t note_class = note % 12;
    const uint8_t root_class = cfg.root_note % 12;
    const uint8_t count = kScaleIntervalCount[id];
    for (uint8_t i = 0; i < count; ++i) {
        if (((root_class + kScaleIntervals[id][i]) % 12) == note_class) {
            return true;
        }
    }
    return false;
}

uint8_t key_filter_apply(uint8_t note, const KeyFilterCfg& cfg, bool& muted) {
    muted = false;
    if (!cfg.enabled || cfg.scale == ScaleId::Off) {
        return note;
    }
    if (note_in_scale(note, cfg)) {
        return note;
    }

    switch (cfg.mode) {
        case SnapMode::SnapUp:
            for (uint16_t n = static_cast<uint16_t>(note) + 1; n <= 127; ++n) {
                if (note_in_scale(static_cast<uint8_t>(n), cfg)) {
                    return static_cast<uint8_t>(n);
                }
            }
            muted = true;
            return note;
        case SnapMode::SnapDown:
            for (int16_t n = static_cast<int16_t>(note) - 1; n >= 0; --n) {
                if (note_in_scale(static_cast<uint8_t>(n), cfg)) {
                    return static_cast<uint8_t>(n);
                }
            }
            muted = true;
            return note;
        case SnapMode::Mute:
        default:
            muted = true;
            return note;
    }
}

uint8_t transpose_compute(uint8_t orig, int offset, uint8_t root,
                          ScaleId scale, SnapMode mode, bool& doomed) {
    doomed = false;
    int v = static_cast<int>(orig) + offset;
    v = std::clamp<int>(v, kNoteRangeMin, kNoteRangeMax);
    const uint8_t n = static_cast<uint8_t>(v);
    if (scale == ScaleId::Off) {
        return n;  // free (chromatic) transpose, clamped to the note range
    }
    KeyFilterCfg cfg;
    cfg.enabled = true;
    cfg.root_note = root % 12;
    cfg.scale = scale;
    cfg.mode = mode;
    if (note_in_scale(n, cfg)) {
        return n;
    }
    if (mode == SnapMode::Mute) {
        doomed = true;  // out-of-scale + Mute -> dropped on commit
        return n;
    }
    return key_filter_apply(n, cfg, doomed);  // SnapUp / SnapDown
}

}  // namespace drom
