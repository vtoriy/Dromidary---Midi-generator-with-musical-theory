#include "usb_midi.hpp"

#include "bsp/board.h"
#include "tusb.h"

namespace drom {

void UsbMidi::init() {
    board_init();
    tusb_init();
}

void UsbMidi::note_on(uint8_t note, uint8_t velocity) const {
    const uint8_t packet[4] = {0x09, 0x90, note, velocity};
    tud_midi_stream_write(0, packet, sizeof(packet));
}

void UsbMidi::note_off(uint8_t note) const {
    const uint8_t packet[4] = {0x08, 0x80, note, 0x00};
    tud_midi_stream_write(0, packet, sizeof(packet));
}

void UsbMidi::cc(uint8_t channel, uint8_t control, uint8_t value) const {
    // Control Change: packet header 0x0B (2 data bytes), status 0xB0 | channel.
    const uint8_t packet[4] = {0x0B, static_cast<uint8_t>(0xB0 | (channel & 0x0F)),
                               control, value};
    tud_midi_stream_write(0, packet, sizeof(packet));
}

}  // namespace drom
