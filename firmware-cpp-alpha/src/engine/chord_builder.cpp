#include "chord_builder.hpp"

#include "key_filter.hpp"

namespace drom {

namespace {

// Interval structure (in semitones from root) for every chord type.
// Indexed by ChordType. Off has a single element {0} (the root alone).
constexpr uint8_t kChordIntervals[][8] = {
    /* Off        */ {0},
    /* Major      */ {0, 4, 7},
    /* Minor      */ {0, 3, 7},
    /* Diminished */ {0, 3, 6},
    /* Augmented  */ {0, 4, 8},
    /* Maj7       */ {0, 4, 7, 11},
    /* Min7       */ {0, 3, 7, 10},
    /* Dom7       */ {0, 4, 7, 10},
    /* Min7b5     */ {0, 3, 6, 10},
    /* Dim7       */ {0, 3, 6, 9},
    /* Chord9     */ {0, 4, 7, 10, 14},
    /* Chord11    */ {0, 4, 7, 10, 14, 17},
    /* Chord13    */ {0, 4, 7, 10, 14, 17, 21},
    /* Maj9       */ {0, 4, 7, 11, 14},
    /* 7#5        */ {0, 4, 8, 10},
    /* 7#9        */ {0, 4, 7, 10, 15},
    /* 7b9        */ {0, 4, 7, 10, 13},
    /* 7#11       */ {0, 4, 7, 10, 18},
    /* Sus2       */ {0, 2, 7},
    /* Sus4       */ {0, 5, 7},
    /* 7sus4      */ {0, 5, 7, 10},
    /* Sus2/7     */ {0, 2, 7, 10},
    /* Quartal    */ {0, 5, 10},
    /* Quintal    */ {0, 7, 14},
    /* Cluster    */ {0, 1, 2, 3},
    /* Power      */ {0, 7},
};

constexpr uint8_t kChordIntervalCount[27] = {
    1, 3, 3, 3, 3, 4, 4, 4, 4, 4, 5, 6, 7, 5, 4, 5, 5, 5, 3, 3, 4, 4, 3, 3, 4, 2,
};

bool chord_enabled(const ChordCfg& cfg) {
    return cfg.enabled && cfg.type != ChordType::Off;
}

}  // namespace

void build_chord(NoteSet& set, uint8_t root, ChordType type) {
    const uint8_t id = static_cast<uint8_t>(type);
    if (id >= static_cast<uint8_t>(ChordType::kCount)) {
        if (set.count < set.notes.size()) {
            set.notes[set.count++] = root;
        }
        return;
    }
    const uint8_t count = kChordIntervalCount[id];
    for (uint8_t i = 0; i < count; ++i) {
        if (set.count >= set.notes.size()) {
            return;
        }
        const uint16_t note = static_cast<uint16_t>(root) + kChordIntervals[id][i];
        if (note <= 127) {
            set.notes[set.count++] = static_cast<uint8_t>(note);
        }
    }
}

NoteSet build_note_set(uint8_t raw_note, const Pattern& pattern) {
    // Transpose, applied before the key filter so a transposed note is also
    // correctly drawn into the scale (see 02-midi-chain.md).
    int16_t note = static_cast<int16_t>(raw_note)
                   + pattern.transpose.semitones
                   + static_cast<int16_t>(pattern.transpose.octaves) * 12;
    if (note < 0) { note = 0; }
    if (note > 127) { note = 127; }
    raw_note = static_cast<uint8_t>(note);

    NoteSet result {};
    bool muted = false;
    const uint8_t snapped = key_filter_apply(raw_note, pattern.key_filter, muted);
    if (muted) {
        return result;
    }

    if (chord_enabled(pattern.chord)) {
        build_chord(result, snapped, pattern.chord.type);
    } else {
        result.notes[0] = snapped;
        result.count = 1;
    }

    // Secondary filter: applies key filter to the whole chord structure,
    // preserving each voice's role (drops notes that snap to nothing).
    if (pattern.key_filter.enabled && pattern.key_filter.scale != ScaleId::Off) {
        NoteSet filtered {};
        for (uint8_t i = 0; i < result.count; ++i) {
            bool m = false;
            const uint8_t s = key_filter_apply(result.notes[i], pattern.key_filter, m);
            if (!m && filtered.count < filtered.notes.size()) {
                filtered.notes[filtered.count++] = s;
            }
        }
        result = filtered;
    }
    return result;
}

}  // namespace drom