import time
from midi_chain import process_note


class Sequencer:
    def __init__(self, state_manager, midi_out):
        self.sm = state_manager
        self.midi = midi_out
        self._last_step_time = 0.0
        self._step_duration_ms = 125.0
        self._scheduled_offs = []

    def start(self):
        self.sm.set_playing(True)
        self.sm.set_recording(False)
        self.sm.set_current_step(0)
        self._last_step_time = time.monotonic()
        self._recalc_step_ms()
        self._scheduled_offs.clear()

    def stop(self):
        self.sm.set_playing(False)
        for n, _, _ in self._scheduled_offs:
            self.midi.note_off(n)
        self._scheduled_offs.clear()

    def toggle_play(self):
        if self.sm.playing:
            self.stop()
        else:
            self.start()

    def _recalc_step_ms(self):
        bpm = self.sm.pattern["bpm"]
        self._step_duration_ms = (60.0 / bpm) * 1000.0 / 4.0

    def tick(self):
        if not self.sm.playing:
            self._check_offs()
            return

        now = time.monotonic()
        elapsed_ms = (now - self._last_step_time) * 1000.0

        if elapsed_ms >= self._step_duration_ms:
            self._last_step_time = now
            self._play_current_step()

    def _play_current_step(self):
        p = self.sm.pattern
        step_idx = self.sm.current_step
        step = self.sm.get_step(step_idx)
        next_step = (step_idx + 1) % p["length"]
        self.sm.set_current_step(next_step)

        if step is None or step["tie"] or not step["active"]:
            return

        if not step["notes"]:
            return

        root = step["notes"][0]
        step_ctx = {
            "step_index": step_idx,
            "ticks_per_step": int(self._step_duration_ms),
            "next_note_on_ticks": int(self._step_duration_ms),
        }

        events = process_note(root, 100, p, step_ctx)
        step_start = time.monotonic()
        for ev in events:
            on_delay = ev["on_offset_ticks"] / 1000.0
            off_delay = ev["off_offset_ticks"] / 1000.0
            on_time = step_start + on_delay
            off_time = step_start + off_delay
            self._scheduled_offs.append(
                (ev["note"], ev["velocity"], off_time)
            )
            self.midi.note_on(ev["note"], ev["velocity"])

    def _check_offs(self):
        now = time.monotonic()
        remaining = []
        for note, vel, off_t in self._scheduled_offs:
            if now >= off_t:
                self.midi.note_off(note)
            else:
                remaining.append((note, vel, off_t))
        self._scheduled_offs = remaining

    def all_notes_off(self):
        for n, _, _ in self._scheduled_offs:
            self.midi.note_off(n)
        self._scheduled_offs.clear()
