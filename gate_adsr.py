def apply_adsr(on_offset_ticks, off_offset_ticks, gate_cfg):
    if not gate_cfg["enabled"]:
        return on_offset_ticks, off_offset_ticks

    attack = gate_cfg.get("attack_ms", 0)
    release = gate_cfg.get("release_ms", 0)

    adjusted_on = on_offset_ticks + attack
    adjusted_off = off_offset_ticks + release

    return adjusted_on, adjusted_off
