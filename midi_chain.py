from key_filter import key_filter
from chord_builder import build_chord, secondary_filter, get_voiced_events
from arpeggiator import generate_arp_cycle, apply_arp_range
from timing_effects import apply_swing, apply_humanize, apply_quantize, apply_legato
from gate_adsr import apply_adsr


def build_note_set(raw_note, pattern):
    """Key Filter -> Chord Builder -> secondary_filter.

    Возвращает список нот (аккорд) для одной сырой ноты, без арпеджиатора.
    Используется process_note (паттерн/live без арпа) и ModeEngine для
    полифонического арпеджио по нескольким зажатым нотам.
    """
    tr = pattern.get("transpose", {"semitones": 0, "octaves": 0})
    note = raw_note + tr.get("semitones", 0) + tr.get("octaves", 0) * 12
    note = max(0, min(127, note))

    key_cfg = pattern["key_filter"]
    snapped = key_filter(note, key_cfg)
    if snapped is None:
        return []

    chord_cfg = pattern["chord_cfg"]
    notes = build_chord(snapped, chord_cfg["type"]) if chord_cfg["enabled"] else [snapped]
    return secondary_filter(notes, key_cfg)


def process_note(raw_note, velocity, pattern, step_ctx, live=False):
    ticks_per_step = step_ctx.get("ticks_per_step", 480)

    arp_cfg = pattern["arp_cfg"]
    chord_cfg = pattern["chord_cfg"]
    timing_cfg = pattern["timing_cfg"]
    gate_cfg = pattern["gate_cfg"]

    step_idx = step_ctx.get("step_index", 0)
    next_on_offset = step_ctx.get("next_note_on_ticks", None)

    notes = build_note_set(raw_note, pattern)
    if not notes:
        return []

    # Арпеджиатор работает и с одиночной нотой: без аккорда база — одна нота,
    # range_semitones добавляет октавы выше (см. 02-midi-chain.md, уточнено).
    if arp_cfg["enabled"]:
        arp_notes = apply_arp_range(notes, arp_cfg.get("range_semitones", 12), 0)
        arp_seq = generate_arp_cycle(arp_notes, arp_cfg["style"], arp_cfg.get("num_steps", 8))
        events = []
        for i, arp_note in enumerate(arp_seq):
            on_tick = i * ticks_per_step
            off_tick = on_tick + ticks_per_step // 2
            if not live:
                on_tick, off_tick = apply_adsr(on_tick, off_tick, gate_cfg)
            events.append({
                "note": arp_note,
                "velocity": velocity,
                "on_offset_ticks": on_tick,
                "off_offset_ticks": off_tick,
            })
        return events

    voiced = get_voiced_events(notes, chord_cfg.get("voicing", "block"),
                                chord_cfg.get("strum_delay_ms", 10))

    events = []
    for vnote, delay in voiced:
        base_on = delay
        base_off = delay + ticks_per_step

        if not live:
            base_on = apply_swing(base_on, timing_cfg.get("swing_pct", 0),
                                   step_idx, ticks_per_step)
            base_on, base_off = apply_humanize(base_on, base_off,
                                                timing_cfg.get("humanize_amount_ms", 0))
            base_on, base_off = apply_quantize(base_on, base_off,
                                                timing_cfg.get("quantize_grid", "off"),
                                                ticks_per_step)
            base_off = apply_legato(base_on, base_off, next_on_offset,
                                     timing_cfg.get("legato", False))
            final_on, final_off = apply_adsr(base_on, base_off, gate_cfg)
        else:
            final_on, final_off = base_on, base_off

        events.append({
            "note": vnote,
            "velocity": velocity,
            "on_offset_ticks": final_on,
            "off_offset_ticks": final_off,
        })

    return events
