#pragma once

#include "../types.hpp"

namespace drom {

// Applies the key filter to a single note.
// Returns the processed note; sets `muted` = true when the note must not sound
// (mode == Mute or no in-scale note found for snap modes).
uint8_t key_filter_apply(uint8_t note, const KeyFilterCfg& cfg, bool& muted);

// True when a note belongs to the configured scale.
bool note_in_scale(uint8_t note, const KeyFilterCfg& cfg);

}  // namespace drom
