#include "shift165.hpp"

#include "hardware/gpio.h"

#include "pico/time.h"

#include "board_pins.hpp"

namespace drom {

namespace {

uint32_t read_once() {
    // Latch the parallel inputs; leave a small settle gap so the input lines
    // stop ringing before the first shift, otherwise individual bits "blink"
    // with 0/1 from wire bounce and crosstalk.
    gpio_put(BoardPins::kShiftLatch, 0);
    sleep_us(1);
    gpio_put(BoardPins::kShiftLatch, 1);
    sleep_us(1);

    uint32_t bits = 0;
    for (uint8_t i = 0; i < 24; ++i) {
        gpio_put(BoardPins::kShiftClock, 0);
        sleep_us(1);  // data bus stable before the sample
        bits = (bits << 1) | static_cast<uint32_t>(gpio_get(BoardPins::kShiftData));
        gpio_put(BoardPins::kShiftClock, 1);
        sleep_us(1);  // clock pulse long enough to shift cleanly
    }
    return bits;
}

}  // namespace

void Shift165::init() {
    gpio_init(BoardPins::kShiftLatch);
    gpio_set_dir(BoardPins::kShiftLatch, GPIO_OUT);

    gpio_init(BoardPins::kShiftClock);
    gpio_set_dir(BoardPins::kShiftClock, GPIO_OUT);

    gpio_init(BoardPins::kShiftData);
    gpio_set_dir(BoardPins::kShiftData, GPIO_IN);
}

uint32_t Shift165::read_all() const {
    // Sample the whole chain twice and retry until two consecutive readings
    // agree. A single bounce mid-daisy-chain can corrupt one bit, and reading
    // it again masks the transition so downstream debouncers see only settled
    // states.
    uint32_t prev = 0;
    for (uint8_t attempt = 0; attempt < 4; ++attempt) {
        const uint32_t cur = read_once();
        if (attempt > 0 && cur == prev) {
            return cur;
        }
        prev = cur;
    }
    return prev;
}

}  // namespace drom
