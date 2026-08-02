import time
from hardware import read_all_chips, read_joystick
from display import DisplayManager
from midi import init_midi
import state
from state_manager import StateManager
from sequencer import Sequencer
from menu import MenuEngine, joystick_dir_to_zone
from renderer import MenuRenderer
from mode_engine import ModeEngine

display_mgr = DisplayManager()
midi = init_midi()
sm = StateManager()
seq = Sequencer(sm, midi)
menu = MenuEngine(sm)
renderer = MenuRenderer(display_mgr.splash)
renderer.render_menu(menu)
mode_eng = ModeEngine(sm, midi, seq)

last_chip1 = 0xFF
last_chip2 = 0xFF
last_chip3 = 0xFF
prev_chip1 = 0xFF
prev_chip2 = 0xFF
prev_chip3 = 0xFF
prev_raw_chip3 = 0xFF
prev_raw_all = 0xFFFFFF
key_held = [False] * 16

last_joy_dir = ""
last_joy_btn = False
last_joy_x = 0
last_joy_y = 0
joy_btn_held_since = 0.0
joy_btn_was_pressed = False
joy_repeat_since = 0.0
joy_dir_held_since = 0.0
joy_repeat_stage = -1
rest_pressed_at = None
rest_prev_held = False
last_short_ts = None
prev_is_rows = False
joystick_dirty = False
display_dirty = True


def _get_input_cfg():
    return state.ensure_slot(state.current_slot).get("input_cfg", {
        "short_press_ms": 500,
        "double_press_ms": 350,
        "long_press_ms": 600,
    })


# Удержание джойстика: интервалы авто-повтора с ускорением (мс).
REPEAT_START_MS = 350   # до начала автоповтора после первого срабатывания


def _joy_repeat_interval(hold_ms):
    if hold_ms < 1000:
        return 150
    if hold_ms < 2000:
        return 80
    if hold_ms < 3000:
        return 40
    return 25


while True:
    raw = read_all_chips()
    raw_chip3 = (raw >> 16) & 0xFF
    chip3 = raw_chip3 & 0xCF  # маска плавающих битов 4-5

    chip2 = (raw >> 8) & 0xFF
    chip1 = raw & 0xFF

    if raw != prev_raw_all:
        print("ALL raw=0x%06X c1=%02X c2=%02X c3=%02X"
              % (raw, chip1, chip2, raw_chip3))

    # functional buttons
    p_pressed = not (chip3 >> 0) & 1
    p_prev = not (prev_chip3 >> 0) & 1
    if p_pressed and not p_prev:
        was_playing = sm.playing
        seq.toggle_play()
        if sm.playing and not was_playing:
            midi.transport_start()
        elif not sm.playing and was_playing:
            midi.transport_stop()
        display_dirty = True

    rec_pressed = not (chip3 >> 2) & 1
    rec_prev = not (prev_chip3 >> 2) & 1
    if rec_pressed and not rec_prev:
        sm.set_recording(not sm.recording)
        midi.transport_continue()
        display_dirty = True

    # Октава: физич. OCTUP -> bit6, физич. OCTDN -> bit7
    # (лог FUNC 02.08.2026: OCTUP гаснет bit6, OCTDN гаснет bit7).
    oct_up = not (chip3 >> 6) & 1
    oct_up_prev = not (prev_chip3 >> 6) & 1
    if oct_up and not oct_up_prev:
        base_octave = state.runtime.get("base_octave", 4)
        if base_octave < 8:
            state.runtime["base_octave"] = base_octave + 1
            display_dirty = True
    oct_dn = not (chip3 >> 7) & 1
    oct_dn_prev = not (prev_chip3 >> 7) & 1
    if oct_dn and not oct_dn_prev:
        base_octave = state.runtime.get("base_octave", 4)
        if base_octave > 1:
            state.runtime["base_octave"] = base_octave - 1
            display_dirty = True

    # Распиновка по логу FUNC 02.08.2026:
    #   bit0=Play  bit1=Rest  bit2=Rec  bit3=Shift  bit6=OctUp(+)  bit7=OctDown(-)
    shift_held = not (chip3 >> 3) & 1
    rest_held = not (chip3 >> 1) & 1

    if chip3 != prev_chip3:
        print("FUNC chip3=0x%02X P=%d REC=%d OCTDN=%d OCTUP=%d SHIFT=%d REST=%d"
              % (chip3,
                 not (chip3 >> 0) & 1, not (chip3 >> 2) & 1,
                 not (chip3 >> 7) & 1, not (chip3 >> 6) & 1,
                 not (chip3 >> 3) & 1, not (chip3 >> 1) & 1))

    # note keys — routed through ModeEngine
    for i in range(16):
        chip = chip1 if i < 8 else chip2
        prev_chip = prev_chip1 if i < 8 else prev_chip2
        bit = i if i < 8 else i - 8
        now = not (chip >> bit) & 1
        before = not (prev_chip >> bit) & 1
        note = (state.runtime.get("base_octave", 4) * 12) + (i % 12)
        if now and not before:
            mode_eng.note_on(i, note, 100)
            key_held[i] = True
        elif not now and before:
            mode_eng.note_off(i, note)
            key_held[i] = False

    prev_chip1, prev_chip2, prev_chip3 = chip1, chip2, chip3
    prev_raw_chip3 = raw_chip3
    prev_raw_all = raw

    # joystick
    joy_dir, joy_btn = read_joystick()
    now = time.monotonic()

    if rest_held and not rest_prev_held:
        rest_pressed_at = now
    rest_prev_held = rest_held

    is_radial = menu.editing_radial
    if joy_dir != last_joy_dir:
        if is_radial and joy_dir != "CENTER":
            zone = joystick_dir_to_zone(joy_dir)
            if zone is not None:
                menu.radial_select(zone)
                display_dirty = True
        else:
            if joy_dir in ("UP", "DOWN", "LEFT", "RIGHT"):
                menu.tilt(joy_dir, shift_held)
                display_dirty = True
        joy_dir_held_since = now
        joy_repeat_since = now
        last_joy_dir = joy_dir
    elif joy_dir in ("UP", "DOWN", "LEFT", "RIGHT") and not is_radial:
        # Удержание джойстика: авто-повтор с ускорением (меню и значения).
        hold_ms = (now - joy_dir_held_since) * 1000
        if (hold_ms > REPEAT_START_MS
                and (now - joy_repeat_since) * 1000 >= _joy_repeat_interval(hold_ms)):
            menu.tilt(joy_dir, shift_held)
            joy_repeat_since = now
            display_dirty = True
    else:
        joy_repeat_since = now

    # joystick button
    if joy_btn and not last_joy_btn:
        joy_btn_held_since = now
        joy_btn_was_pressed = True
    elif not joy_btn and last_joy_btn:
        if joy_btn_was_pressed:
            hold_ms = (now - joy_btn_held_since) * 1000
            icfg = _get_input_cfg()
            rest_click = (rest_pressed_at is not None
                          and (now - rest_pressed_at) * 1000 < icfg.get("long_press_ms", 800))
            if rest_click:
                menu.reset_value()
                last_short_ts = None
            elif hold_ms < icfg.get("short_press_ms", 500):
                if (not menu.is_rows and last_short_ts is not None
                        and (now - last_short_ts) < icfg.get("double_press_ms", 350) / 1000):
                    menu.reset_value()
                    last_short_ts = None
                else:
                    menu.press_short()
                    last_short_ts = now
            else:
                menu.press_long()
                last_short_ts = None
            display_dirty = True
        joy_btn_was_pressed = False

    last_joy_btn = joy_btn

    if menu.is_rows != prev_is_rows:
        last_short_ts = None
    prev_is_rows = menu.is_rows

    # render
    if display_dirty or sm.current_step != state.runtime.get("_last_rendered_step", -1):
        mode = state.runtime["screen_mode"]
        if mode == "animation":
            renderer.render_animation(time.monotonic())
        else:
            renderer.render_menu(menu)
        state.runtime["_last_rendered_step"] = sm.current_step
        display_dirty = False

    seq.tick()
    mode_eng.tick()
    time.sleep(0.01)
