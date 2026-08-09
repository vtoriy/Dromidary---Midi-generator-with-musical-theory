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
}  // namespace

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
