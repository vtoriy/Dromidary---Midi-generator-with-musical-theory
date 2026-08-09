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