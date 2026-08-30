#pragma once

#include "../types.hpp"

namespace drom {

// Arp note-rate divisions ("1/64".."1/1"), index into kArpNoteDivs.
// Includes triplet subdivisions (1/3, 1/6, 1/12, 1/24, 1/48).
constexpr const char* kArpNoteDivs[] = {
    "1/64", "1/48", "1/32", "1/24", "1/16", "1/12",
    "1/8", "1/6", "1/4", "1/3", "1/2", "1/1",
};
constexpr int kArpNoteDivCount = 12;

// Random-note gate-length divisions for the LEN cell, compact labels: plain
// digits replace the "1/" prefix ("16" = 1/16); an apostrophe marks whole-note
// multiples ("'1" = 1/1 whole, "'2" = 2 wholes, "'4" = 4 wholes).
constexpr const char* kNoteLenDivs[] = {
    "128", "96", "64", "48", "32", "24",
    "16", "12", "8", "6", "4", "3", "2", "'1", "'2", "'4",
};
constexpr int kNoteLenDivCount = 16;

// Real kNoteLenDivs index of the default note duration (1/16 = index 6, "16").
// A freshly placed note (and any erased step) uses this as its length, so a
// note recorded after an erase is 1/16 rather than inheriting the old one.
constexpr uint8_t kNoteLenDiv1_16 = 6;

// Duration of one gate-length division in ms at the given BPM.
uint32_t note_len_ms(uint8_t div_index, uint16_t bpm);

// Beats (quarter-note units) of one gate-length division; index-aligned with
// kNoteLenDivs (0 = 1/128 .. 15 = '4). Exposed so the pattern editor can draw
// a duration tail proportional to the TRUE length instead of the index order.
float note_len_div_beats(uint8_t div_index);

// LEN list visibility: with triplets off only straight divisions are offered.
int note_len_div_count(bool triplets);
// Map a visible position (0..count-1) to the real kNoteLenDivs index.
uint8_t note_len_div_real(int pos, bool triplets);
// Map a real index to the nearest visible position (triplets snap down).
int note_len_div_pos(uint8_t real_idx, bool triplets);

// Arp interval in ms for the current rate config (note divisions honour BPM).
uint16_t arp_interval_ms(const ArpCfg& cfg, uint16_t bpm);

// Quantize-grid divisions in beats (index 0 = Off), params to kQuantizeLabels.
constexpr float kQuantizeGridBeats[] = {
    0.0f,          // Off
    0.03125f,      // 1/32
    0.041666667f,  // 1/16T
    0.0625f,       // 1/16
    0.083333333f,  // 1/8T
    0.125f,        // 1/8
    0.166666667f,  // 1/4T
    0.25f,         // 1/4
};
constexpr int kQuantizeGridCount = 8;

// Duration of a single quantize grid cell in ms for the current BPM; 0 = Off.
uint32_t quantize_grid_ms(uint8_t grid_index, uint16_t bpm);

inline bool quantize_grid_active(uint8_t grid_index) {
    return grid_index > 0 && grid_index < kQuantizeGridCount;
}

}  // namespace drom