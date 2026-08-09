#pragma once

#include <cstdint>

namespace drom {

class UsbMidi {
public:
    void init();
    void note_on(uint8_t note, uint8_t velocity) const;
    void note_off(uint8_t note) const;
    void cc(uint8_t channel, uint8_t control, uint8_t value) const;
};

}  // namespace drom
