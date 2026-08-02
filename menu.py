import state

JOY_CENTER = 32767
RADIAL_DEADZONE = 10000
LONG_PRESS_MS = 600

NOTE_NAMES = ["C","C#","D","D#","E","F","F#","G","G#","A","A#","B"]
ROOT_OPTIONS = list(range(12))

SCALE_RADIAL = ["off", "major", "minor", "dorian", "phrygian",
                "lydian", "mixolydian", "blues"]
SCALE_RADIAL_LABELS = ["Off", "Maj", "Min", "Dor", "Phr", "Lyd", "Mix", "Blu"]

CTYPE_RADIAL = ["off", "major", "minor", "maj7", "min7", "dom7", "sus4", "power"]
CTYPE_RADIAL_LABELS = ["Off", "Maj", "Min", "Maj7", "Min7", "7", "Sus4", "Pow"]

ASTYLE_RADIAL = ["off", "up", "down", "up_down", "down_up",
                 "as_played", "random", "converge_diverge"]
ASTYLE_RADIAL_LABELS = ["Off", "Up", "Down", "UpDn", "DnUp", "Play", "Rnd", "CvDv"]

STRUM_ZONES = [0, 5, 10, 15, 20, 25, 30, 35]
STRUM_LABELS = ["Off", "5", "10", "15", "20", "25", "30", "35"]

ARP_NOTE_DIVS = ["1/64", "1/32", "1/16", "1/8", "1/4", "1/2", "1/1"]
ARP_NOTE_DIV_LABELS = {"1/64": "1/64", "1/32": "1/32", "1/16": "1/16",
                       "1/8": "1/8", "1/4": "1/4", "1/2": "1/2", "1/1": "1/1"}

SCALE_LABEL_MAP = {"off": "Off", "major": "Maj", "minor": "Min", "dorian": "Dor",
                   "phrygian": "Phr", "lydian": "Lyd", "mixolydian": "Mix",
                   "blues": "Blu"}
CTYPE_LABEL_MAP = {"off": "Off", "major": "Maj", "minor": "Min", "maj7": "Maj7",
                   "min7": "Min7", "dom7": "7", "sus4": "Sus4", "power": "Pow"}
VOICING_LABEL_MAP = {"block": "Blk", "strum": "Strm", "roll": "Roll"}
ASTYLE_LABEL_MAP = {"off": "Off", "up": "Up", "down": "Down", "up_down": "UpDn",
                    "down_up": "DnUp", "as_played": "Play", "random": "Rnd",
                    "converge_diverge": "CvDv"}

SHAPE_OPTIONS = ["ascending", "descending", "arch", "rnd_walk"]
SHAPE_LABELS = {"ascending": "Asc", "descending": "Desc", "arch": "Arch",
                "rnd_walk": "Rnd"}

SCALE_FULL = ["off", "major", "minor", "dorian", "phrygian", "lydian",
              "mixolydian", "locrian", "harmonic_minor", "melodic_minor",
              "pentatonic_major", "pentatonic_minor", "blues", "whole_tone",
              "diminished"]
SCALE_FULL_LABELS = {"off": "Off", "major": "Maj", "minor": "Min", "dorian": "Dor",
                     "phrygian": "Phr", "lydian": "Lyd", "mixolydian": "Mix",
                     "locrian": "Loc", "harmonic_minor": "HMin", "melodic_minor": "MMin",
                     "pentatonic_major": "PMaj", "pentatonic_minor": "PMin",
                     "blues": "Blu", "whole_tone": "WhT", "diminished": "Dim"}

CTYPE_FULL = ["off", "major", "minor", "diminished", "augmented", "maj7", "min7",
              "dom7", "min7b5", "dim7", "chord_9", "chord_11", "chord_13", "maj9",
              "7sh5", "7sh9", "7b9", "7sh11", "sus2", "sus4", "7sus4", "sus2_7",
              "quartal", "quintal", "cluster", "power"]
CTYPE_FULL_LABELS = {"off": "Off", "major": "Maj", "minor": "Min", "diminished": "Dim",
                     "augmented": "Aug", "maj7": "Maj7", "min7": "Min7", "dom7": "7",
                     "min7b5": "m7b5", "dim7": "Dim7", "chord_9": "9", "chord_11": "11",
                     "chord_13": "13", "maj9": "Maj9", "7sh5": "7#5", "7sh9": "7#9",
                     "7b9": "7b9", "7sh11": "7#11", "sus2": "Sus2", "sus4": "Sus4",
                     "7sus4": "7s4", "sus2_7": "s2/7", "quartal": "Qrt", "quintal": "Qnt",
                     "cluster": "Cls", "power": "Pow"}

ASTYLE_FULL = ["off", "up", "down", "up_down", "down_up", "as_played",
               "random", "converge_diverge"]
ASTYLE_FULL_LABELS = {"off": "Off", "up": "Up", "down": "Down", "up_down": "UpDn",
                      "down_up": "DnUp", "as_played": "Play", "random": "Rnd",
                      "converge_diverge": "CvDv"}


def _scale_zone(kf):
    if not kf["enabled"]:
        return 0
    try:
        return SCALE_RADIAL.index(kf["scale"])
    except ValueError:
        return 1


def _apply_scale_zone(kf, zone):
    if 0 <= zone < len(SCALE_RADIAL):
        kf["scale"] = SCALE_RADIAL[zone]
        kf["enabled"] = zone != 0


def _ctype_zone(cc):
    if not cc["enabled"]:
        return 0
    try:
        return CTYPE_RADIAL.index(cc["type"])
    except ValueError:
        return 1


def _apply_ctype_zone(cc, zone):
    if 0 <= zone < len(CTYPE_RADIAL):
        if zone == 0:
            cc["enabled"] = False
        else:
            cc["enabled"] = True
            cc["type"] = CTYPE_RADIAL[zone]


def _astyle_zone(ac):
    if not ac["enabled"]:
        return 0
    try:
        return ASTYLE_RADIAL.index(ac["style"])
    except ValueError:
        return 1


def _apply_astyle_zone(ac, zone):
    if 0 <= zone < len(ASTYLE_RADIAL):
        if zone == 0:
            ac["enabled"] = False
        else:
            ac["enabled"] = True
            ac["style"] = ASTYLE_RADIAL[zone]


def _strum_zone(cc):
    return max(0, min(len(STRUM_ZONES) - 1, cc["strum_delay_ms"] // 5))


def _apply_strum_zone(cc, zone):
    if 0 <= zone < len(STRUM_ZONES):
        cc["strum_delay_ms"] = STRUM_ZONES[zone]


def _adsr_summary(p):
    gc = p["gate_cfg"]
    return "On" if gc["enabled"] else "Off"


def _scale_full_val(kf):
    return "off" if not kf["enabled"] else kf["scale"]


def _apply_scale_full(kf, v):
    kf["enabled"] = v != "off"
    if v != "off":
        kf["scale"] = v


def _ctype_full_val(cc):
    return "off" if not cc["enabled"] else cc["type"]


def _apply_ctype_full(cc, v):
    cc["enabled"] = v != "off"
    if v != "off":
        cc["type"] = v


def _astyle_full_val(ac):
    return "off" if not ac["enabled"] else ac["style"]


def _apply_astyle_full(ac, v):
    ac["enabled"] = v != "off"
    if v != "off":
        ac["style"] = v


def _rate_getter(ac):
    rate = ac["rate"]
    if rate.get("unit") == "ms":
        try:
            return int(rate["value"])
        except (ValueError, TypeError):
            return 100
    return rate.get("value", "1/8")


def _rate_setter(ac, v):
    rate = ac["rate"]
    if rate.get("unit") == "ms":
        rate.update({"unit": "ms", "value": int(v)})
    else:
        rate.update({"unit": "note", "value": v})


def _rate_set_unit(ac, unit):
    rate = ac["rate"]
    if unit == "ms":
        try:
            val = int(rate.get("value", 100))
        except (ValueError, TypeError):
            val = 100
        rate.update({"unit": "ms", "value": val})
    else:
        val = rate.get("value", "1/8")
        if not isinstance(val, str) or val not in ARP_NOTE_DIVS:
            val = "1/8"
        rate.update({"unit": "note", "value": val})


def _key_children(kf):
    return [
        option("Key", lambda: kf["root_note"],
               lambda v: kf.update({"root_note": v}),
               ROOT_OPTIONS, lambda i: NOTE_NAMES[i]),
        option("Scale", lambda: _scale_full_val(kf),
               lambda v: _apply_scale_full(kf, v),
               SCALE_FULL, label_map=SCALE_FULL_LABELS),
        option("Snap", lambda: kf["mode"],
               lambda v: kf.update({"mode": v}),
               ["snap_up", "snap_down", "mute"]),
    ]


def _chord_children(cc):
    return [
        option("CType", lambda: _ctype_full_val(cc),
               lambda v: _apply_ctype_full(cc, v),
               CTYPE_FULL, label_map=CTYPE_FULL_LABELS),
        option("CVoic", lambda: cc["voicing"],
               lambda v: cc.update({"voicing": v}),
               ["block", "strum", "roll"]),
        int_slider("Strum", lambda: cc["strum_delay_ms"],
                   lambda v: cc.update({"strum_delay_ms": v}), 1, 100),
    ]


def _arp_children(ac):
    return [
        option("AStyle", lambda: _astyle_full_val(ac),
               lambda v: _apply_astyle_full(ac, v),
               ASTYLE_FULL, label_map=ASTYLE_FULL_LABELS),
        toggle("Latch", lambda: ac.get("latch", False),
               lambda v: ac.update({"latch": v})),
        option("RateMode", lambda: ac["rate"].get("unit", "note"),
               lambda v: _rate_set_unit(ac, v),
               ["note", "ms"], label_map={"note": "Note", "ms": "Ms"}),
        rate_item("Rate",
                  lambda: _rate_getter(ac),
                  lambda v: _rate_setter(ac, v),
                  lambda: ac["rate"].get("unit", "note"),
                  lambda v: _rate_set_unit(ac, v),
                  ARP_NOTE_DIVS, ARP_NOTE_DIV_LABELS, 10, 2000, 10),
        int_slider("ARange", lambda: ac["range_semitones"],
                   lambda v: ac.update({"range_semitones": v}), 0, 48),
        int_slider("ASteps", lambda: ac["num_steps"],
                   lambda v: ac.update({"num_steps": v}), 1, 32),
    ]


def _adsr_children(gc):
    return [
        toggle("Switch", lambda: gc["enabled"],
               lambda v: gc.update({"enabled": v})),
        int_slider("Atk", lambda: gc["attack_ms"],
                   lambda v: gc.update({"attack_ms": v}), 0, 500),
        int_slider("Dec", lambda: gc["decay_ms"],
                   lambda v: gc.update({"decay_ms": v}), 0, 500),
        int_slider("Sus", lambda: gc["sustain_pct"],
                   lambda v: gc.update({"sustain_pct": v}), 0, 100),
        int_slider("Rel", lambda: gc["release_ms"],
                   lambda v: gc.update({"release_ms": v}), 0, 500),
        option("Sync", lambda: gc["sync"],
               lambda v: gc.update({"sync": v}), ["ms", "quantize"]),
    ]


def quick_rows(sm):
    mode = state.runtime["mode"]
    p = sm.pattern
    kf = p["key_filter"]
    cc = p["chord_cfg"]
    ac = p["arp_cfg"]
    gc = p["gate_cfg"]
    rc = p["random_cfg"]

    rows = []

    rows.append(row("Key", [
        linear_seg("Key", lambda: kf["root_note"],
                   lambda v: kf.update({"root_note": v}),
                   ROOT_OPTIONS, lambda i: NOTE_NAMES[i]),
        radial_seg("Scale", lambda: _scale_zone(kf),
                   lambda z: _apply_scale_zone(kf, z),
                   SCALE_RADIAL_LABELS),
        param_seg("Param", _key_children(kf)),
    ]))

    if mode in ("midi_keyboard", "random_note", "midi_filter"):
        rows.append(row("Chord", [
            radial_seg("CType", lambda: _ctype_zone(cc),
                       lambda z: _apply_ctype_zone(cc, z),
                       CTYPE_RADIAL_LABELS),
            radial_seg("Strum", lambda: _strum_zone(cc),
                       lambda z: _apply_strum_zone(cc, z),
                       STRUM_LABELS),
            param_seg("Param", _chord_children(cc)),
        ]))

    if mode in ("midi_keyboard", "midi_filter"):
        rows.append(row("Arp", [
            radial_seg("AStyle", lambda: _astyle_zone(ac),
                       lambda z: _apply_astyle_zone(ac, z),
                       ASTYLE_RADIAL_LABELS),
            linear_seg("Latch", lambda: bool(ac.get("latch", False)),
                       lambda v: ac.update({"latch": bool(v)}),
                       [False, True], lambda v: "On" if v else "Off"),
            param_seg("Param", _arp_children(ac)),
        ]))

    rows.append(row("ADSR", [],
                    submenu=_adsr_children(gc),
                    summary_fn=lambda: _adsr_summary(p)))

    if mode in ("random_pattern", "random_note"):
        rows.append(row("Dens", [
            linear_seg("Dens", lambda: int(rc["density_or_probability"] * 100),
                       lambda v: rc.update({"density_or_probability": v / 100.0}),
                       list(range(0, 101))),
        ]))
        rows.append(row("Shape", [
            linear_seg("Shape", lambda: rc["shape"],
                       lambda v: rc.update({"shape": v}),
                       SHAPE_OPTIONS,
                       lambda i: SHAPE_LABELS.get(SHAPE_OPTIONS[i], SHAPE_OPTIONS[i])),
        ]))

    return rows


def full_menu_items(sm):
    p = sm.pattern
    return [
        section("Pattern", [
            int_slider("Slot", lambda: state.current_slot,
                       lambda v: sm.switch_slot(v), 0, 15),
            option("Length", lambda: p["length"], lambda v: sm.resize_pattern(v),
                   [16, 32, 48, 64]),
            int_slider("BPM", lambda: p["bpm"], lambda v: p.update({"bpm": v}), 20, 300),
            action("Clear Pattern", lambda: sm.clear_slot()),
        ]),
        section("Key / Scale", [
            toggle("Key Filter", lambda: p["key_filter"]["enabled"],
                   lambda v: p["key_filter"].update({"enabled": v})),
            option("Root Note", lambda: p["key_filter"]["root_note"],
                   lambda v: p["key_filter"].update({"root_note": v}),
                   ROOT_OPTIONS, lambda i: NOTE_NAMES[i]),
            option("Scale", lambda: p["key_filter"]["scale"],
                   lambda v: p["key_filter"].update({"scale": v}),
                   ["major","minor","dorian","phrygian","lydian","mixolydian","locrian",
                    "harmonic_minor","melodic_minor","pentatonic_major","pentatonic_minor",
                    "blues","whole_tone","diminished"]),
            option("Snap", lambda: p["key_filter"]["mode"],
                   lambda v: p["key_filter"].update({"mode": v}),
                   ["snap_up","snap_down","mute"]),
        ]),
        section("Chord", [
            toggle("Chord", lambda: p["chord_cfg"]["enabled"],
                   lambda v: p["chord_cfg"].update({"enabled": v})),
            option("Type", lambda: p["chord_cfg"]["type"],
                   lambda v: p["chord_cfg"].update({"type": v}),
                   ["major","minor","diminished","augmented","maj7","min7","dom7",
                    "min7b5","dim7","chord_9","chord_11","chord_13","maj9",
                    "7sh5","7sh9","7b9","7sh11",
                    "sus2","sus4","7sus4","sus2_7",
                    "quartal","quintal","cluster","power"]),
            option("Voicing", lambda: p["chord_cfg"]["voicing"],
                   lambda v: p["chord_cfg"].update({"voicing": v}),
                   ["block","strum","roll"]),
            int_slider("Strum", lambda: p["chord_cfg"]["strum_delay_ms"],
                       lambda v: p["chord_cfg"].update({"strum_delay_ms": v}), 1, 100),
        ]),
        section("Arpeggiator", [
            toggle("Arp", lambda: p["arp_cfg"]["enabled"],
                   lambda v: p["arp_cfg"].update({"enabled": v})),
            toggle("Latch", lambda: p["arp_cfg"].get("latch", False),
                   lambda v: p["arp_cfg"].update({"latch": v})),
            option("Style", lambda: p["arp_cfg"]["style"],
                   lambda v: p["arp_cfg"].update({"style": v}),
                   ["up","down","up_down","down_up","as_played","random","converge_diverge"]),
            option("RateMode", lambda: p["arp_cfg"]["rate"].get("unit", "note"),
                   lambda v: _rate_set_unit(p["arp_cfg"], v),
                   ["note", "ms"], label_map={"note": "Note", "ms": "Ms"}),
            rate_item("Rate",
                      lambda: _rate_getter(p["arp_cfg"]),
                      lambda v: _rate_setter(p["arp_cfg"], v),
                      lambda: p["arp_cfg"]["rate"].get("unit", "note"),
                      lambda v: _rate_set_unit(p["arp_cfg"], v),
                      ARP_NOTE_DIVS, ARP_NOTE_DIV_LABELS, 10, 2000, 10),
            int_slider("Range", lambda: p["arp_cfg"]["range_semitones"],
                       lambda v: p["arp_cfg"].update({"range_semitones": v}), 0, 48),
            int_slider("Steps", lambda: p["arp_cfg"]["num_steps"],
                       lambda v: p["arp_cfg"].update({"num_steps": v}), 1, 32),
        ]),
        section("Timing", [
            int_slider("Swing", lambda: p["timing_cfg"]["swing_pct"],
                       lambda v: p["timing_cfg"].update({"swing_pct": v}), 0, 100),
            int_slider("Humanize", lambda: p["timing_cfg"]["humanize_amount_ms"],
                       lambda v: p["timing_cfg"].update({"humanize_amount_ms": v}), 0, 50),
            option("Quantize", lambda: p["timing_cfg"]["quantize_grid"],
                   lambda v: p["timing_cfg"].update({"quantize_grid": v}),
                   ["off","1/32","1/16T","1/16","1/8T","1/8","1/4T","1/4"]),
            toggle("Legato", lambda: p["timing_cfg"]["legato"],
                   lambda v: p["timing_cfg"].update({"legato": v})),
        ]),
        section("Gate / ADSR", [
            toggle("ADSR", lambda: p["gate_cfg"]["enabled"],
                   lambda v: p["gate_cfg"].update({"enabled": v})),
            int_slider("Attack", lambda: p["gate_cfg"]["attack_ms"],
                       lambda v: p["gate_cfg"].update({"attack_ms": v}), 0, 200),
            int_slider("Release", lambda: p["gate_cfg"]["release_ms"],
                       lambda v: p["gate_cfg"].update({"release_ms": v}), 0, 500),
        ]),
        section("Transpose", [
            int_slider("Semitones", lambda: p["transpose"]["semitones"],
                       lambda v: p["transpose"].update({"semitones": v}), -12, 12),
            int_slider("Octaves", lambda: p["transpose"]["octaves"],
                       lambda v: p["transpose"].update({"octaves": v}), -4, 4),
        ]),
        section("Octave", [
            int_slider("Base Octave", lambda: state.runtime.get("base_octave", 4),
                       lambda v: state.runtime.update({"base_octave": v}), 1, 8),
        ]),
        section("MIDI", [
            toggle("USB MIDI", lambda: True, lambda v: None),
            int_slider("Channel", lambda: 1, lambda v: None, 1, 16),
        ]),
        section("Input", [
            int_slider("Short ms", lambda: p["input_cfg"]["short_press_ms"],
                       lambda v: p["input_cfg"].update({"short_press_ms": v}), 200, 1000),
            int_slider("Double ms", lambda: p["input_cfg"]["double_press_ms"],
                       lambda v: p["input_cfg"].update({"double_press_ms": v}), 100, 600),
            int_slider("Long ms", lambda: p["input_cfg"]["long_press_ms"],
                       lambda v: p["input_cfg"].update({"long_press_ms": v}), 400, 2000),
        ]),
    ]


# --- Row / segment builders (Quick Level 1 cells) ---

def row(label, segments, submenu=None, summary_fn=None):
    return {"type": "row", "label": label, "segments": segments,
            "submenu": submenu, "summary_fn": summary_fn}


def linear_seg(label, getter, setter, options, label_fn=None, direct=False):
    return {"type": "linear", "label": label, "getter": getter, "setter": setter,
            "options": options, "label_fn": label_fn, "direct": direct}


def radial_seg(label, getter, setter, labels):
    return {"type": "radial", "label": label, "getter": getter, "setter": setter,
            "labels": labels}


def param_seg(label, children):
    return {"type": "param", "label": label, "children": children}


# --- Submenu / full-menu item builders ---

def section(label, children):
    return {"type": "section", "label": label, "children": children}


def group(label, summary_fn, children):
    return {"type": "group", "label": label, "summary_fn": summary_fn,
            "children": children}


def toggle(label, getter, setter):
    return {"type": "toggle", "label": label, "getter": getter, "setter": setter}


def option(label, getter, setter, options, label_fn=None, click_first=False, label_map=None):
    return {"type": "option", "label": label, "getter": getter, "setter": setter,
            "options": options, "label_fn": label_fn, "click_first": click_first,
            "label_map": label_map}


def radial_option(label, getter, setter, labels):
    return {"type": "option", "label": label, "getter": getter, "setter": setter,
            "options": labels, "radial": True}


def int_slider(label, getter, setter, min_v, max_v):
    return {"type": "int_slider", "label": label, "getter": getter, "setter": setter,
            "min": min_v, "max": max_v}


def rate_item(label, getter, setter, unit_getter, unit_setter,
              note_divs, note_label_map, ms_min, ms_max, ms_step=10):
    return {"type": "rate", "label": label, "getter": getter, "setter": setter,
            "unit_getter": unit_getter, "unit_setter": unit_setter,
            "note_divs": note_divs, "note_label_map": note_label_map,
            "min": ms_min, "max": ms_max, "ms_step": ms_step}


def action(label, fn):
    return {"type": "action", "label": label, "action": fn}


# --- Display helpers ---

def item_display(item):
    t = item["type"]
    if t == "toggle":
        return "On" if item["getter"]() else "Off"
    elif t == "group":
        return item["summary_fn"]()
    elif t == "option":
        val = item["getter"]()
        opts = item["options"]
        fn = item.get("label_fn")
        lm = item.get("label_map")
        if isinstance(val, int):
            if fn:
                return fn(val) if 0 <= val < len(opts) else str(val)
            elif 0 <= val < len(opts):
                return str(opts[val])
        elif lm and val in lm:
            return lm[val]
        return str(val)
    elif t == "int_slider":
        return str(item["getter"]())
    elif t == "rate":
        if item["unit_getter"]() == "ms":
            return f"{int(item['getter']())}ms"
        val = item["getter"]()
        lm = item["note_label_map"]
        return lm.get(val, str(val))
    return ""


def item_value(item):
    t = item["type"]
    if t == "toggle":
        return "On" if item["getter"]() else "Off"
    elif t == "option":
        val = item["getter"]()
        opts = item["options"]
        label_fn = item.get("label_fn")
        lm = item.get("label_map")
        if isinstance(val, int) and label_fn:
            return label_fn(val) if 0 <= val < len(opts) else str(val)
        if not isinstance(val, int) and lm and val in lm:
            return lm[val]
        return str(val)
    elif t == "int_slider":
        return str(item["getter"]())
    elif t == "rate":
        if item["unit_getter"]() == "ms":
            return f"{int(item['getter']())}ms"
        val = item["getter"]()
        lm = item["note_label_map"]
        return lm.get(val, str(val))
    elif t == "section":
        return ">"
    return ""


def item_set_value(item, delta):
    t = item["type"]
    if t == "toggle":
        item["setter"](not item["getter"]())
    elif t == "option":
        opts = item["options"]
        cur = item["getter"]()
        if isinstance(cur, int):
            idx = cur
        else:
            try:
                idx = opts.index(cur)
            except ValueError:
                idx = 0
        new_idx = max(0, min(len(opts) - 1, idx + delta))
        new_val = new_idx if isinstance(cur, int) else opts[new_idx]
        item["setter"](new_val)
    elif t == "int_slider":
        cur = item["getter"]()
        new_val = max(item["min"], min(item["max"], cur + delta))
        item["setter"](new_val)
    elif t == "rate":
        if item["unit_getter"]() == "ms":
            step = item.get("ms_step", 10)
            cur = int(item["getter"]())
            new_val = max(item["min"], min(item["max"], cur + delta * step))
            item["setter"](new_val)
        else:
            opts = item["note_divs"]
            cur = item["getter"]()
            try:
                idx = opts.index(cur)
            except ValueError:
                idx = 0
            new_idx = max(0, min(len(opts) - 1, idx + delta))
            item["setter"](opts[new_idx])


def segment_value(seg):
    t = seg["type"]
    if t == "linear":
        val = seg["getter"]()
        opts = seg["options"]
        fn = seg.get("label_fn")
        if isinstance(val, int):
            if fn:
                return fn(val) if 0 <= val < len(opts) else str(val)
            return str(opts[val]) if 0 <= val < len(opts) else str(val)
        try:
            idx = opts.index(val)
        except ValueError:
            return str(val)
        return fn(idx) if fn else str(val)
    elif t == "radial":
        idx = seg["getter"]()
        labels = seg["labels"]
        return labels[idx] if 0 <= idx < len(labels) else "?"
    elif t == "param":
        return "PRM"
    return ""


def seg_set_value(seg, delta):
    t = seg["type"]
    if t == "linear":
        opts = seg["options"]
        cur = seg["getter"]()
        if isinstance(cur, int):
            new_idx = max(0, min(len(opts) - 1, cur + delta))
            seg["setter"](new_idx)
        else:
            try:
                idx = opts.index(cur)
            except ValueError:
                idx = 0
            new_idx = max(0, min(len(opts) - 1, idx + delta))
            seg["setter"](opts[new_idx])


DIR_TO_ZONE = {
    "UP": 0, "UP-RIGHT": 1, "RIGHT": 2, "DOWN-RIGHT": 3,
    "DOWN": 4, "DOWN-LEFT": 5, "LEFT": 6, "UP-LEFT": 7,
}


def joystick_dir_to_zone(joy_dir):
    return DIR_TO_ZONE.get(joy_dir)


def _item_shift(item, direction):
    t = item["type"]
    to_min = direction == "LEFT"
    if t == "int_slider":
        item["setter"](item["min"] if to_min else item["max"])
    elif t == "rate":
        if item["unit_getter"]() == "ms":
            item["setter"](item["min"] if to_min else item["max"])
        else:
            opts = item["note_divs"]
            item["setter"](opts[0] if to_min else opts[-1])
    elif t == "option":
        opts = item["options"]
        if isinstance(item["getter"](), int):
            item["setter"](0 if to_min else len(opts) - 1)
        else:
            item["setter"](opts[0] if to_min else opts[-1])


class MenuEngine:
    def __init__(self, sm):
        self.sm = sm
        self.stack = []
        self._edit_mode = False
        self._edit_snapshot = None
        self._submenu_snapshot = None
        self._item_snapshot = {}
        self._build_root()

    def _build_root(self):
        mode = state.runtime["screen_mode"]
        if mode == "quick":
            rows = quick_rows(self.sm)
            self.stack = [("rows", rows, 0, 0, 0)]
        elif mode == "full":
            items = full_menu_items(self.sm)
            self.stack = [("items", items, 0, 0)]
        else:
            self.stack = []
        self._edit_mode = False
        self._edit_snapshot = None
        self._submenu_snapshot = None
        self._item_snapshot = {}
        if self.stack:
            self._snapshot_focused()

    def rebuild(self):
        self._build_root()

    @property
    def is_rows(self):
        return bool(self.stack) and self.stack[-1][0] == "rows"

    @property
    def items(self):
        return self.stack[-1][1]

    @property
    def current_item(self):
        idx = self.stack[-1][2]
        if 0 <= idx < len(self.items):
            return self.items[idx]
        return None

    @property
    def current_row(self):
        return self.current_item if self.is_rows else None

    @property
    def current_segment(self):
        if not self.is_rows:
            return None
        row = self.current_row
        if row is None:
            return None
        segs = row["segments"]
        if not segs:
            return None
        seg_idx = self.stack[-1][4]
        if 0 <= seg_idx < len(segs):
            return segs[seg_idx]
        return None

    @property
    def editing_radial(self):
        seg = self.current_segment
        return self._edit_mode and seg is not None and seg["type"] == "radial"

    def move_row(self, delta):
        if not self.is_rows:
            return
        items = self.items
        if not items:
            return
        _t, _items, cur, off, _seg = self.stack[-1]
        max_i = len(items) - 1
        new_cur = max(0, min(max_i, cur + delta))
        new_off = off
        if new_cur < off:
            new_off = new_cur
        elif new_cur >= off + 5:
            new_off = new_cur - 4
        self.stack[-1] = ("rows", items, new_cur, new_off, 0)

    def move_seg(self, delta):
        if not self.is_rows:
            return
        segs = self.current_row["segments"] if self.current_row else []
        if not segs:
            return
        _t, _items, cur, off, seg_idx = self.stack[-1]
        new_idx = max(0, min(len(segs) - 1, seg_idx + delta))
        self.stack[-1] = ("rows", self.items, cur, off, new_idx)

    def move_cursor(self, delta):
        if self.is_rows:
            self.move_row(delta)
            return
        items = self.items
        if not items:
            return
        max_i = len(items) - 1
        _t, _items, cur, off = self.stack[-1]
        new_cur = max(0, min(max_i, cur + delta))
        new_off = off
        if new_cur < off:
            new_off = new_cur
        elif new_cur >= off + 4:
            new_off = new_cur - 3
        # Snapshot old focused item before leaving it (accept pending change)
        if cur != new_cur:
            old_item = items[cur] if 0 <= cur < len(items) else None
            if old_item is not None and old_item.get("getter") is not None:
                self._item_snapshot[id(old_item)] = old_item["getter"]()
        self.stack[-1] = ("items", items, new_cur, new_off)
        if new_cur != cur:
            self._snapshot_focused()

    def _enter_edit(self, seg):
        self._edit_mode = True
        self._edit_snapshot = seg["getter"]()

    def _push_items(self, items):
        self.stack.append(("items", items, 0, 0))
        self._item_snapshot = {}
        self._submenu_snapshot = {
            id(it): it["getter"]()
            for it in items
            if it.get("getter") is not None
        }
        self._snapshot_focused()

    def _snapshot_focused(self):
        if self.is_rows:
            return
        item = self.current_item
        if item is not None and item.get("getter") is not None:
            self._item_snapshot[id(item)] = item["getter"]()

    def press_short(self):
        if self.is_rows:
            seg = self.current_segment
            row = self.current_row
            if self._edit_mode:
                self._edit_mode = False
                self._edit_snapshot = None
            elif seg is None:
                sub = row.get("submenu") if row else None
                if sub:
                    self._push_items(sub)
            elif seg["type"] == "param":
                self._push_items(seg["children"])
            elif seg["type"] in ("linear", "radial"):
                self._enter_edit(seg)
            return

        item = self.current_item
        if item is None:
            return
        t = item["type"]
        if t in ("section", "group"):
            children = item["children"]
            if children:
                self._push_items(children)
        elif t == "action":
            item["action"]()

    def press_long(self):
        if len(self.stack) > 1:
            self.stack.pop()
            self._edit_mode = False
            self._edit_snapshot = None
            self._submenu_snapshot = None
            self._item_snapshot = {}
            self._snapshot_focused()
        else:
            modes = ["quick", "full", "animation"]
            cur = state.runtime["screen_mode"]
            try:
                idx = modes.index(cur)
            except ValueError:
                idx = 0
            state.runtime["screen_mode"] = modes[(idx + 1) % len(modes)]
            self._build_root()

    def reset_value(self):
        if self.is_rows:
            seg = self.current_segment
            if self._edit_mode and seg is not None and self._edit_snapshot is not None:
                seg["setter"](self._edit_snapshot)
        else:
            item = self.current_item
            if item is None:
                return
            snap = self._item_snapshot.get(id(item))
            setter = item.get("setter")
            if snap is not None and setter is not None:
                setter(snap)

    def tilt(self, direction, shift_held=False):
        if self.is_rows:
            if self._edit_mode:
                seg = self.current_segment
                if seg and seg["type"] == "linear" and direction in ("LEFT", "RIGHT"):
                    if shift_held:
                        opts = seg["options"]
                        if isinstance(seg["getter"](), int):
                            seg["setter"](0 if direction == "LEFT" else len(opts) - 1)
                        else:
                            seg["setter"](opts[0 if direction == "LEFT" else -1])
                    else:
                        seg_set_value(seg, 1 if direction == "RIGHT" else -1)
                return
            if direction in ("UP", "DOWN"):
                self.move_row(1 if direction == "DOWN" else -1)
                return
            if direction in ("LEFT", "RIGHT"):
                seg = self.current_segment
                if seg and seg.get("direct"):
                    if shift_held:
                        opts = seg["options"]
                        if isinstance(seg["getter"](), int):
                            seg["setter"](0 if direction == "LEFT" else len(opts) - 1)
                        else:
                            seg["setter"](opts[0 if direction == "LEFT" else -1])
                    else:
                        seg_set_value(seg, 1 if direction == "RIGHT" else -1)
                else:
                    self.move_seg(1 if direction == "RIGHT" else -1)
            return

        item = self.current_item
        if item is None:
            return
        if item["type"] in ("section", "group"):
            if direction in ("UP", "DOWN"):
                self.move_cursor(1 if direction == "DOWN" else -1)
            return

        if direction in ("UP", "DOWN"):
            self.move_cursor(1 if direction == "DOWN" else -1)
            return

        if item.get("radial"):
            return

        if direction == "LEFT":
            if shift_held:
                _item_shift(item, "LEFT")
            else:
                item_set_value(item, -1)
        elif direction == "RIGHT":
            if shift_held:
                _item_shift(item, "RIGHT")
            else:
                item_set_value(item, 1)

    def radial_select(self, zone):
        if self.is_rows:
            seg = self.current_segment
            if seg and seg["type"] == "radial" and self._edit_mode:
                seg["setter"](zone)
            return
        item = self.current_item
        if item is None or item["type"] != "option":
            return
        opts = item["options"]
        if 0 <= zone < len(opts):
            item["setter"](zone if isinstance(item["getter"](), int) else opts[zone])
