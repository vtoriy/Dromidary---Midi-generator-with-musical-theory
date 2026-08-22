#pragma once

#include <cstdint>

namespace drom {

class UsbMidi {
public:
    void init();
    void note_on(uint8_t note, uint8_t velocity) const;
    void note_off(uint8_t note) const;
    void cc(uint8_t channel, uint8_t control, uint8_t value) const;
    // MIDI real-time system messages (single-byte): 0xF8 clock, 0xFA start,
    // 0xFB continue, 0xFC stop.
    void realtime(uint8_t status) const;
    // Poll the host->device stream for the next realtime status byte, or 0 if
    // nothing arrived. Non-realtime packets are drained and ignored.
    uint8_t poll_realtime() const;
};

}  // namespace drom
