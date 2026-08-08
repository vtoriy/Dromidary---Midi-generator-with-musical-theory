#include "midi_chain.hpp"

namespace drom {

namespace {
// Beats per note division (mirrors mode_engine.py _NOTE_DIV_BEATS).
constexpr float kArpNoteDivBeats[] = {
    0.0625f,  // 1/64
    0.125f,   // 1/32
    0.25f,    // 1/16
    0.5f,     // 1/8
    1.0f,     // 1/4
    2.0f,     // 1/2
    4.0f,     // 1/1
};
}  // namespace

uint16_t arp_interval_ms(const ArpCfg& cfg, uint16_t bpm) {
    if (cfg.rate_mode == RateMode::Ms) {
        uint16_t ms = cfg.rate_ms;
        if (ms < 20) { ms = 20; }
        return ms;
    }
    uint8_t idx = cfg.rate_note_index;
    if (idx >= kArpNoteDivCount) { idx = 2; }
    const float beats = kArpNoteDivBeats[idx];
    float bpm_f = bpm;
    if (bpm_f < 20.0f) { bpm_f = 120.0f; }
    uint16_t ms = static_cast<uint16_t>((60.0f / bpm_f) * 1000.0f * beats);
    if (ms < 20) { ms = 20; }
    return ms;
}

}  // namespace drom
