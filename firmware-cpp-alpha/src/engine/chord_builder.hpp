#pragma once

#include "../types.hpp"

namespace drom {

// Key Filter -> Chord Builder -> secondary_filter (whole structure re-filtered).
// Returns the set of midi notes for a single raw note, without arpeggiator.
NoteSet build_note_set(uint8_t raw_note, const Pattern& pattern);

// Builds a chord by interval structure from a root note (no key filter).
void build_chord(NoteSet& set, uint8_t root, ChordType type);

}  // namespace drom