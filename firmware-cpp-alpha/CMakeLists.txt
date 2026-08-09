cmake_minimum_required(VERSION 3.13)

include(pico_sdk_import.cmake)

project(dromidary_cpp_alpha C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

pico_sdk_init()

add_executable(dromidary_cpp_alpha
    src/main.cpp
    src/app_loop.cpp
    src/persist.cpp
    src/state/state.cpp
    src/engine/key_filter.cpp
    src/engine/chord_builder.cpp
    src/engine/arpeggiator.cpp
    src/engine/midi_chain.cpp
    src/mode/mode_engine.cpp
    src/menu/menu_items.cpp
    src/menu/menu_engine.cpp
    src/menu/renderer.cpp
    src/menu/animation.cpp
    src/platform/shift165.cpp
    src/platform/usb_midi.cpp
    src/platform/display_sh1106.cpp
    src/platform/joystick.cpp
    src/usb_descriptors.c
)

target_include_directories(dromidary_cpp_alpha PRIVATE src)

target_link_libraries(dromidary_cpp_alpha
    pico_stdlib
    hardware_gpio
    hardware_i2c
    hardware_adc
    hardware_flash
    hardware_exception
    pico_flash
    tinyusb_device
    tinyusb_board
)

pico_enable_stdio_uart(dromidary_cpp_alpha 0)
pico_enable_stdio_usb(dromidary_cpp_alpha 0)

pico_add_extra_outputs(dromidary_cpp_alpha)
