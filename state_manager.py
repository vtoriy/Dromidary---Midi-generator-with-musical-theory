import state


class StateManager:
    @property
    def pattern(self):
        return state.ensure_slot(state.current_slot)

    @property
    def slot_index(self):
        return state.current_slot

    @property
    def playing(self):
        return state.runtime["playing"]

    @property
    def recording(self):
        return state.runtime["recording"]

    @property
    def current_step(self):
        return state.runtime["current_step"]

    def switch_slot(self, index):
        if 0 <= index < state.NUM_SLOTS:
            state.current_slot = index
            state.ensure_slot(index)
            return True
        return False

    def clear_slot(self, slot_index=None):
        idx = slot_index if slot_index is not None else state.current_slot
        if 0 <= idx < state.NUM_SLOTS:
            state.slots[idx] = state.make_pattern()
            return True
        return False

    def resize_pattern(self, new_length):
        p = self.pattern
        if new_length not in (16, 32, 48, 64):
            return False
        while len(p["steps"]) < new_length:
            p["steps"].append(state.make_empty_step())
        while len(p["steps"]) > new_length:
            p["steps"].pop()
        p["length"] = new_length
        return True

    def get_step(self, step_idx):
        p = self.pattern
        if 0 <= step_idx < len(p["steps"]):
            return p["steps"][step_idx]
        return None

    def set_step(self, step_idx, step_data):
        p = self.pattern
        if 0 <= step_idx < len(p["steps"]):
            p["steps"][step_idx] = step_data
            return True
        return False

    def toggle_step_active(self, step_idx):
        step = self.get_step(step_idx)
        if step is not None:
            step["active"] = not step["active"]
            return True
        return False

    def toggle_step_tie(self, step_idx):
        step = self.get_step(step_idx)
        if step is not None:
            step["tie"] = not step["tie"]
            return True
        return False

    def clear_all_patterns(self):
        for i in range(state.NUM_SLOTS):
            state.slots[i] = state.make_pattern()

    def set_mode(self, mode):
        allowed = ("midi_keyboard", "pattern", "random_pattern",
                   "random_note", "midi_filter")
        if mode in allowed:
            state.runtime["mode"] = mode
            return True
        return False

    def set_playing(self, value):
        state.runtime["playing"] = bool(value)

    def set_recording(self, value):
        state.runtime["recording"] = bool(value)

    def set_current_step(self, step_idx):
        state.runtime["current_step"] = int(step_idx)

    def set_screen_mode(self, mode):
        allowed = ("quick", "full", "animation")
        if mode in allowed:
            state.runtime["screen_mode"] = mode
            return True
        return False

    def push_menu(self, item):
        state.runtime["menu_stack"].append(item)

    def pop_menu(self):
        if state.runtime["menu_stack"]:
            return state.runtime["menu_stack"].pop()
        return None
