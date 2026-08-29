# dromidary C++ Firmware

`firmware-cpp-alpha` is the C/C++ implementation of the `dromidary` MIDI
groovebox for Raspberry Pi Pico (RP2040). It replaces the CircuitPython build:
typed static state, deterministic input scanning, full Quick/Detail/Main menu,
joystick navigation, and the live MIDI chain.

## Scope

Implemented:

- typed static state (`struct`/`enum`, no dynamic allocation)
- 24-button scanning through a 74HC165 cascade (16 note keys + 6 function keys)
- KY-023 joystick (8-way navigation, click, long-press)
- SH1106 128x64 OLED driver (I2C, 5x7 font, framebuffer + flush)
- Quick (Level 1) panel with click-edit cells, radial zones and Detail submenus
- Full MAIN menu tree (Pattern, Key/Scale, Chord, Arpeggiator, Timing, Gate/ADSR,
  Transpose, Octave, MIDI)
- screen modes: QUICK / DETAIL / MAIN / ANIMATION; long-press cycles
  QUICK <-> MAIN, the QUICK header click cycles the play mode KB/RND/PTRN
- key filter (16 scales, snap up/down/mute), chord builder (26 chord types),
  polyphonic arpeggiator (19 styles incl. Chord-trigger, note-division or ms
  rate with triplets, keys×range keyboard-column expansion, scale filtering,
  cycle length, latch)
- RandomNote mode: continuous random-note loop with a PITCH range, a LEN gate-
  length range (triplets optional), REP anchor-repeat chance and Len Chain
  (duration-driven timing vs ARP Rate grid)
- RandomPattern mode: one-shot random pattern generated into the active slot
  from a key press, looped as a duration chain (rests by Density, pattern
  length STEP 4..64, Gate % articulation, Regen action)
- ADR quick row: On/Atk/Dec cells (full ADSR in DETAIL); ALL quick row jumps
  to the FULL menu root
- MIDI Clock master/slave over USB (drift-free master accumulator; slave BPM
  estimated from host F8 stream)
- screensaver animation with auto-start on idle and manual launch, each entry
  starting from a randomized scene stage
- timing FX (swing/humanize/quantize/legato) applied to the live arpeggio,
  Gate/ADSR (attack delays Note On, release extends Note Off) and chord voicing
  Block/Strum/Roll via a delayed-event queue in `ModeEngine`
- timing quick row (Swing/Quantize + DETAIL) and BPM moved into `TimingCfg`
  (Quick → TIM; FULL → Timing section)
- joystick tilt auto-repeat with hold-time acceleration (220 ms → ~60 ms, x3
  steps on the fastest tier)
- USB MIDI note on/off path + function keys duplicated as MIDI CC (20–25, ch. 16)
- persisted settings (debounce/double/long/idle) in the last flash sector
- raw-input **Test** screen (System → Test), exit with Shift + joystick click

Deferred:

- Pattern mode playback/recording/step-edit for recorded patterns
- MIDI Filter mode (needs UART MIDI IN)
- UART MIDI DIN
- flash persistence for patterns
- wiring Shape/Dens into the generation engine

## What you need to install (to build and flash)

### Toolchain

1. **CMake** 3.13+ — https://cmake.org/download/ (Windows installer adds it to PATH)
2. **Ninja** — `winget install Ninja-build.Ninja` or https://ninja-build.org/
3. **ARM GCC cross-compiler** (arm-none-eabi) — "Arm GNU Toolchain" for Windows
   from https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads
   (pick the `arm-none-eabi` variant, latest 13.2/13.3 or newer).
4. **Raspberry Pi Pico SDK** — clone from GitHub:

   ```sh
   git clone -b 1.5.1 --recurse-submodules https://github.com/raspberrypi/pico-sdk.git
   ```

   Then set the environment variable so CMake can find it:

   ```sh
   # PowerShell
   setx PICO_SDK_PATH "C:\path\to\pico-sdk"
   # restart the terminal after setx
   ```

   (On Linux/macOS: `export PICO_SDK_PATH=/path/to/pico-sdk`.)

### Verify

From a new terminal:

```sh
cmake --version
ninja --version
arm-none-eabi-gcc --version
echo $env:PICO_SDK_PATH
```

## Build

```sh
cmake -B build -G Ninja
cmake --build build
```

Output: `build/dromidary_cpp_alpha.uf2`.

## Flash

1. Hold the **BOOTSEL** button on the Pico while plugging the USB cable in.
2. A USB mass-storage drive `RPI-RP2` appears.
3. Copy `dromidary_cpp_alpha.uf2` onto it. The Pico reboots automatically.

The device enumerates as a **USB MIDI** peripheral; connect it to a DAW or MIDI
monitor and play the 16 note keys. OLED + joystick + function buttons work out
of the box with the pinned wiring (see `src/platform/board_pins.hpp`).

## Wiring reference (unchanged)

- SH1106 OLED: I2C0 — SDA = GP0, SCL = GP1, addr 0x3C
- 74HC165 chain: Latch = GP2, Clock = GP3, Data = GP4
- KY-023 joystick: X = GP26 (ADC0), Y = GP27 (ADC1), button = GP15
