import random

QUANTIZE_GRIDS = {
    "1/32":  (1 / 32),
    "1/16T": (1 / 24),
    "1/16":  (1 / 16),
    "1/8T":  (1 / 12),
    "1/8":   (1 / 8),
    "1/4T":  (1 / 6),
    "1/4":   (1 / 4),
}


def apply_swing(on_offset_ticks, swing_pct, step_idx, ticks_per_step):
    if swing_pct == 0:
        return on_offset_ticks

    if step_idx % 2 == 1:
        swing_amount = ticks_per_step * (swing_pct / 100.0) * 0.5
        return on_offset_ticks + swing_amount

    return on_offset_ticks


def apply_humanize(on_offset_ticks, off_offset_ticks, amount_ticks):
    if amount_ticks == 0:
        return on_offset_ticks, off_offset_ticks

    jitter = random.randint(-amount_ticks, amount_ticks)
    return on_offset_ticks + jitter, off_offset_ticks + jitter


def apply_quantize(on_offset_ticks, off_offset_ticks, grid, ticks_per_step):
    if grid == "off" or grid is None:
        return on_offset_ticks, off_offset_ticks

    grid_fraction = QUANTIZE_GRIDS.get(grid)
    if grid_fraction is None:
        return on_offset_ticks, off_offset_ticks

    grid_ticks = int(ticks_per_step * grid_fraction * 4)

    if grid_ticks < 1:
        grid_ticks = 1

    q_on = round(on_offset_ticks / grid_ticks) * grid_ticks
    q_off = round(off_offset_ticks / grid_ticks) * grid_ticks

    return q_on, q_off


def apply_legato(on_offset_ticks, off_offset_ticks, next_on_offset_ticks, legato_on):
    if not legato_on:
        return off_offset_ticks

    if next_on_offset_ticks is None:
        return off_offset_ticks

    if off_offset_ticks < next_on_offset_ticks:
        return next_on_offset_ticks

    return off_offset_ticks
