#pragma once

#include "../types.hpp"

namespace drom {

// Applies the key filter to a single note.
// Returns the processed note; sets `muted` = true when the note must not sound
// (mode == Mute or no in-scale note found for snap modes).
uint8_t key_filter_apply(uint8_t note, const KeyFilterCfg& cfg, bool& muted);

// True when a note belongs to the configured scale.
bool note_in_scale(uint8_t note, const KeyFilterCfg& cfg);

// Computes the pitch after transposing `orig` by `offset` semitones, optionally
// constrained to a target tonality (root/scale from the editor's transpose
// tool). Returns the resulting pitch, or sets `doomed`=true (and returns orig)
// when the note falls out of the scale under SnapMode::Mute — i.e. it should be
// skipped/deleted on commit. A scale of Off applies the plain clamped shift.
uint8_t transpose_compute(uint8_t orig, int offset, uint8_t root,
                          ScaleId scale, SnapMode mode, bool& doomed);

}  // namespace drom
