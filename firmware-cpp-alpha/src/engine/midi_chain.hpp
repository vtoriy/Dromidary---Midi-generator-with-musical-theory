#pragma once

#include "../types.hpp"

namespace drom {

// Arp note-rate divisions ("1/64".."1/1"), index into kArpNoteDivs.
constexpr const char* kArpNoteDivs[] = {
    "1/64", "1/32", "1/16", "1/8", "1/4", "1/2", "1/1",
};
constexpr int kArpNoteDivCount = 7;

// Arp interval in ms for the current rate config (note divisions honour BPM).
uint16_t arp_interval_ms(const ArpCfg& cfg, uint16_t bpm);

}  // namespace drom