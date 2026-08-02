from adafruit_display_text import label
from terminalio import FONT
import state
from menu import item_display, segment_value

CHAR_W = 6
ROW_H = 10
MARGIN_X = 2

LABEL_W = 6                     # chars, columns aligned to grid
COL0_X = MARGIN_X + LABEL_W * CHAR_W          # first value cell
COL0_W = 4 * CHAR_W
COL1_X = COL0_X + COL0_W + 2                  # second value cell (shifted left)
COL1_W = 4 * CHAR_W
PARAM_X = COL1_X + COL1_W                     # PRM cell
PARAM_W = 6 * CHAR_W
SCREEN_W = 132

LABEL_SHORT = {"Chord": "CHD", "ADSR": "ADR"}


class MenuRenderer:
    def __init__(self, splash):
        self.splash = splash

    def clear(self):
        while len(self.splash) > 0:
            self.splash.pop()

    def render_menu(self, engine):
        self.clear()
        status = self._build_status()
        self.splash.append(label.Label(FONT, text=status, color=0xFFFFFF, x=2, y=4))

        line = 0
        if len(engine.stack) > 1:
            frame = engine.stack[-2]
            parent_items, parent_idx = frame[1], frame[2]
            parent = parent_items[parent_idx]
            back = "<" + LABEL_SHORT.get(parent["label"], parent["label"])
            self.splash.append(label.Label(FONT, text=back, color=0xFFFFFF, x=2, y=14))
            line = 1

        if engine.is_rows:
            self._render_rows(engine, line)
        else:
            self._render_items(engine, line)

    def _render_rows(self, engine, line):
        _t, items, cur, offset, seg_idx = engine.stack[-1]
        vis_start = offset
        vis_end = min(offset + (5 - line), len(items))
        for i in range(vis_start, vis_end):
            item = items[i]
            focused = i == cur
            y = 14 + line * ROW_H

            lbl = LABEL_SHORT.get(item["label"], item["label"]) + ":"
            cells = [lbl]
            bases = [MARGIN_X]

            segs = item["segments"]
            if not segs:
                summary = item.get("summary_fn")
                txt = summary() if summary else ""
                if focused:
                    txt = "[" + txt + "]"
                cells.append(txt)
                bases.append(COL0_X)
            else:
                col = 0
                for si, seg in enumerate(segs):
                    is_param = seg["type"] == "param"
                    if is_param:
                        base_x = PARAM_X
                        txt = "PRM"
                    elif col == 0:
                        base_x = COL0_X
                        txt = segment_value(seg)
                    else:
                        base_x = COL1_X
                        txt = segment_value(seg)

                    if focused and si == seg_idx:
                        if not is_param:
                            changed = (engine._edit_mode
                                       and engine._edit_snapshot is not None
                                       and seg["getter"]() != engine._edit_snapshot)
                            mark = "!" if changed else ""
                            txt = "[" + txt + "]" + mark
                        else:
                            txt = "[" + txt + "]"
                    cells.append(txt)
                    bases.append(base_x)
                    if not is_param:
                        col += 1

            xs = []
            prev_right = 0
            for j, txt in enumerate(cells):
                base = bases[j]
                x = base if base >= prev_right + 2 else prev_right + 2
                xs.append(x)
                prev_right = x + len(txt) * CHAR_W
            if prev_right > SCREEN_W:
                dx = min(prev_right - SCREEN_W, min(xs) - MARGIN_X)
                xs = [x - dx for x in xs]
            for j, txt in enumerate(cells):
                x = xs[j]
                if x + len(txt) * CHAR_W > SCREEN_W:
                    txt = txt[: max(1, (SCREEN_W - x) // CHAR_W)]
                self.splash.append(label.Label(FONT, text=txt, color=0xFFFFFF,
                                               x=x, y=y))
            line += 1

    def _render_items(self, engine, line):
        _t, items, cur, offset = engine.stack[-1]
        item_snap = getattr(engine, "_item_snapshot", None)
        vis_start = offset
        vis_end = min(offset + (5 - line), len(items))
        for i in range(vis_start, vis_end):
            item = items[i]
            cur_i = i == cur
            prefix = ">" if cur_i else " "
            changed_prefix = ""
            changed_suffix = ""
            if item_snap and id(item) in item_snap and item.get("getter") is not None:
                cur_val = item["getter"]()
                snap_val = item_snap[id(item)]
                try:
                    if cur_val > snap_val:
                        changed_suffix = "+"
                    elif cur_val < snap_val:
                        changed_prefix = "-"
                except TypeError:
                    pass
            base = self._item_text(item)
            # Insert +/- adjacent to value part (after ": ")
            if ": " in base:
                label_part, value_part = base.split(": ", 1)
                signs_len = len(changed_prefix) + len(changed_suffix)
                # Total available for label + ": " + value + signs = 22 - len(prefix)
                max_total = 22 - len(prefix)
                label_val_len = len(label_part) + 2 + len(value_part) + signs_len
                if label_val_len > max_total:
                    # Need to truncate: first try truncating value, then label
                    value_budget = max_total - len(label_part) - 2 - signs_len
                    if value_budget >= 1:
                        # Can fit label + ": " + truncated value + signs
                        if changed_prefix or changed_suffix:
                            value_part = "~"
                        else:
                            value_part = value_part[:value_budget - 1] + "~"
                    else:
                        # Label too long, truncate label
                        label_budget = max_total - 2 - len(value_part) - signs_len
                        if label_budget < 1:
                            label_budget = 1
                        label_part = label_part[:label_budget - 1] + "~"
                base = f"{label_part}: {changed_prefix}{value_part}{changed_suffix}"
            else:
                max_base = 22 - len(prefix)
                if len(base) > max_base:
                    base = base[: max(1, max_base - 1)] + "~"
            txt = f"{prefix}{base}"
            if len(txt) > 22:
                txt = txt[:21] + "~"
            l = label.Label(FONT, text=txt, color=0xFFFFFF, x=2, y=14 + line * 10)
            self.splash.append(l)
            line += 1

    def _item_text(self, item):
        t = item["type"]
        if t == "group":
            val = item_display(item)
            suffix = " | PRM" if item["children"] else ""
            return f"{item['label']}: {val}{suffix}"
        if t == "section":
            return f"{item['label']}>"
        val = item_display(item)
        return f"{item['label']}: {val}" if val else item["label"]

    def render_animation(self, t):
        self.clear()
        seed = int(t * 3) % 60
        info = label.Label(FONT, text="dromidary", color=0xFFFFFF, x=30, y=28)
        self.splash.append(info)
        star = label.Label(FONT, text="." * ((seed // 6) % 6), color=0xFFFFFF, x=seed * 2 % 120, y=seed % 50)
        self.splash.append(star)
        bpm = state.ensure_slot(state.current_slot)["bpm"]
        f = label.Label(FONT, text=f"{bpm}bpm", color=0xFFFFFF, x=2, y=58)
        self.splash.append(f)

    def _build_status(self):
        mode = state.runtime["screen_mode"]
        prefix = {"quick": "Quick |", "full": "Main |", "animation": "Anim |"}.get(mode, "Quick |")
        p = state.ensure_slot(state.current_slot)
        play = ">" if state.runtime["playing"] else "."
        rec = "R" if state.runtime["recording"] else "."
        bpm = p["bpm"]
        step = state.runtime["current_step"]
        octv = state.runtime.get("base_octave", 4)
        return f"{prefix} {play}{rec} o{octv} {bpm} s{step}"
