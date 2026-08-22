#include "midi_chain.hpp"

namespace drom {

namespace {
// Beats per note division (a "1/N" note lasts 4/N quarter-note beats).
constexpr float kArpNoteDivBeats[] = {
    0.0625f,        // 1/64
    0.083333333f,   // 1/48 (триоль)
    0.125f,         // 1/32
    0.166666667f,   // 1/24 (триоль)
    0.25f,          // 1/16
    0.333333333f,   // 1/12 (триоль)
    0.5f,           // 1/8
    0.666666667f,   // 1/6 (триоль)
    1.0f,           // 1/4
    1.333333333f,   // 1/3 (триоль)
    2.0f,           // 1/2
    4.0f,           // 1/1
};
constexpr int kArpNoteDivBeatsCount = 12;

// Beats per gate-length division; index-aligned with kNoteLenDivs.
constexpr float kNoteLenDivBeats[] = {
    0.03125f,       // 128 (1/128)
    0.041666667f,   // 96 (1/96, триоль)
    0.0625f,        // 64 (1/64)
    0.083333333f,   // 48 (1/48, триоль)
    0.125f,         // 32 (1/32)
    0.166666667f,   // 24 (1/24, триоль)
    0.25f,          // 16 (1/16)
    0.333333333f,   // 12 (1/12, триоль)
    0.5f,           // 8 (1/8)
    0.666666667f,   // 6 (1/6, триоль)
    1.0f,           // 4 (1/4)
    1.333333333f,   // 3 (1/3, триоль)
    2.0f,           // 2 (1/2)
    4.0f,           // '1 (целая)
    8.0f,           // '2 (две целых)
    16.0f,          // '4 (четыре целых)
};
}  // namespace

uint32_t note_len_ms(uint8_t div_index, uint16_t bpm) {
    uint8_t idx = div_index;
    if (idx >= kNoteLenDivCount) { idx = 6; }  // fallback "16" (1/16)
    float bpm_f = bpm;
    if (bpm_f < 20.0f) { bpm_f = 120.0f; }
    const uint32_t ms =
        static_cast<uint32_t>((60.0f / bpm_f) * 1000.0f * kNoteLenDivBeats[idx]);
    return ms < 1 ? 1 : ms;
}

// Straight divisions only: real indices into kNoteLenDivs without the
// triplet entries (96, 48, 24, 12, 6, 3).
constexpr uint8_t kStraightLenIdx[] = {0, 2, 4, 6, 8, 10, 12, 13, 14, 15};

int note_len_div_count(bool triplets) {
    return triplets ? kNoteLenDivCount
                    : static_cast<int>(sizeof(kStraightLenIdx) / sizeof(kStraightLenIdx[0]));
}

uint8_t note_len_div_real(int pos, bool triplets) {
    if (triplets) {
        if (pos < 0) { pos = 0; }
        if (pos >= kNoteLenDivCount) { pos = kNoteLenDivCount - 1; }
        return static_cast<uint8_t>(pos);
    }
    const int n = static_cast<int>(sizeof(kStraightLenIdx) / sizeof(kStraightLenIdx[0]));
    if (pos < 0) { pos = 0; }
    if (pos >= n) { pos = n - 1; }
    return kStraightLenIdx[pos];
}

int note_len_div_pos(uint8_t real_idx, bool triplets) {
    if (triplets) {
        return (real_idx < kNoteLenDivCount) ? static_cast<int>(real_idx)
                                             : kNoteLenDivCount - 1;
    }
    // Nearest straight division at or below the requested index.
    int best = 0;
    const int n = static_cast<int>(sizeof(kStraightLenIdx) / sizeof(kStraightLenIdx[0]));
    for (int i = 0; i < n; ++i) {
        if (kStraightLenIdx[i] <= real_idx) {
            best = i;
        }
    }
    return best;
}

uint16_t arp_interval_ms(const ArpCfg& cfg, uint16_t bpm) {
    if (cfg.rate_mode == RateMode::Ms) {
        uint16_t ms = cfg.rate_ms;
        if (ms < 20) { ms = 20; }
        return ms;
    }
    uint8_t idx = cfg.rate_note_index;
    if (idx >= kArpNoteDivBeatsCount) { idx = 6; }  // fallback "1/8"
    const float beats = kArpNoteDivBeats[idx];
    float bpm_f = bpm;
    if (bpm_f < 20.0f) { bpm_f = 120.0f; }
    uint16_t ms = static_cast<uint16_t>((60.0f / bpm_f) * 1000.0f * beats);
    if (ms < 20) { ms = 20; }
    return ms;
}

uint32_t quantize_grid_ms(uint8_t grid_index, uint16_t bpm) {
    if (grid_index >= kQuantizeGridCount) {
        return 0;
    }
    const float beats = kQuantizeGridBeats[grid_index];
    if (beats <= 0.0f) {
        return 0;
    }
    float bpm_f = bpm;
    if (bpm_f < 20.0f) { bpm_f = 120.0f; }
    uint32_t ms = static_cast<uint32_t>((60.0f / bpm_f) * 1000.0f * beats);
    return ms < 1 ? 1 : ms;
}

}  // namespace drom
