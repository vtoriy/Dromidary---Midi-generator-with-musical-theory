def make_step(notes=None, active=True, tie=False, length_steps=4):
    return {
        "notes": notes if notes is not None else [],
        "active": active,
        "tie": tie,
        "length_steps": length_steps,
    }


def make_empty_step():
    return make_step(notes=[], active=False, tie=False, length_steps=4)


def make_pattern(steps=None, length=16):
    if steps is None:
        steps = [make_empty_step() for _ in range(length)]
    return {
        "steps": steps,
        "length": length,
        "bpm": 120,
        "time_signature": (4, 4),
        "key_filter": {
            "enabled": False,
            "root_note": 0,
            "scale": "off",
            "mode": "snap_up",
        },
        "chord_cfg": {
            "enabled": False,
            "type": "major",
            "voicing": "block",
            "strum_delay_ms": 10,
        },
        "arp_cfg": {
            "enabled": False,
            "latch": False,
            "rate": {"unit": "note", "value": "1/8"},
            "range_semitones": 12,
            "num_steps": 8,
            "style": "up",
        },
        "timing_cfg": {
            "swing_pct": 0,
            "humanize_amount_ms": 0,
            "quantize_grid": "off",
            "legato": False,
        },
        "gate_cfg": {
            "enabled": False,
            "attack_ms": 0,
            "decay_ms": 0,
            "sustain_pct": 100,
            "release_ms": 0,
            "sync": "ms",
            "quantize_den": 16,
        },
        "transpose": {
            "semitones": 0,
            "octaves": 0,
        },
        "random_cfg": {
            "note_length": {"mode": "fixed", "min": 4, "max": 8, "unit": "steps"},
            "note_range": {
                "mode": "simple",
                "proximity": "medium",
                "manual_min": 36,
                "manual_max": 96,
            },
            "density_or_probability": 0.5,
            "shape": "ascending",
            "velocity_mode": {
                "mode": "off",
                "off_value": 100,
                "min": 80,
                "max": 120,
                "delta": 10,
            },
        },
        "input_cfg": {
            "short_press_ms": 500,
            "double_press_ms": 350,
            "long_press_ms": 600,
        },
    }


NUM_SLOTS = 16
# Ленивое создание слотов: при загрузке выделяем только слот 0, остальные
# создаются по мере обращения (ensure_slot). Экономит ~45 КБ heap на Pico.
slots = [None] * NUM_SLOTS
slots[0] = make_pattern()
current_slot = 0


def ensure_slot(index):
    if 0 <= index < NUM_SLOTS and slots[index] is None:
        slots[index] = make_pattern()
    return slots[index]


runtime = {
    "mode": "midi_keyboard",
    "playing": False,
    "recording": False,
    "current_step": 0,
    "screen_mode": "quick",
    "menu_stack": [],
    "live_mute": False,
    "base_octave": 4,
}


def active_pattern():
    return ensure_slot(current_slot)


def switch_slot(index):
    global current_slot
    if 0 <= index < NUM_SLOTS:
        current_slot = index
        ensure_slot(index)
        return True
    return False
