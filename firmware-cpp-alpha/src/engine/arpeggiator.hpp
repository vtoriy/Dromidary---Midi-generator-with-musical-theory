#pragma once

#include "../types.hpp"

namespace drom {

// Deterministic PRNG used by the Random arp style and random-note helpers.
class SimpleRng {
public:
    explicit SimpleRng(uint32_t seed = 0x9E3779B9u);
    void seed(uint32_t s);
    uint32_t next();          // 0..0xFFFFFFFF
    uint32_t range(uint32_t max);  // 0..max-1
    int32_t range_int(int32_t lo, int32_t hi);  // lo..hi inclusive

private:
    uint32_t state_ {0};
};

class Arpeggiator {
public:
    void reset();

    // Merged/ordered input set -> arp cycle (range applied, style applied).
    // `num_steps` limits the cycle length (kept >= 1).
    void rebuild(const NoteSet& input, const ArpCfg& cfg);

    bool active() const;
    uint8_t next_note();

private:
    std::array<uint8_t, kMaxArpNotes> sequence_ {};
    uint8_t count_ {0};
    uint8_t index_ {0};
};

// Builds the full arp cycle (keyboard-column replicas -> scale filter -> style)
// into `out`. Used both by the live scheduler and by pattern processing.
void build_arp_sequence(const NoteSet& base, const ArpCfg& cfg, const KeyFilterCfg& key_filter,
                        std::array<uint8_t, kMaxArpNotes>& out, uint8_t& out_count);

}  // namespace drom
