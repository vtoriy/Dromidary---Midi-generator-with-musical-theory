import random
import time
import state
from midi_chain import process_note, build_note_set
from arpeggiator import generate_arp_cycle, apply_arp_range


_MODES = ("midi_keyboard", "pattern", "random_pattern", "random_note", "midi_filter")

_ARP_MODES = ("midi_keyboard", "midi_filter")

_NOTE_DIV_BEATS = {
    "1/64": 0.0625,
    "1/32": 0.125,
    "1/16": 0.25,
    "1/8": 0.5,
    "1/4": 1.0,
    "1/2": 2.0,
    "1/1": 4.0,
}


class ModeEngine:
    def __init__(self, sm, midi, seq):
        self.sm = sm
        self.midi = midi
        self.seq = seq
        self._held_notes = {}
        self._last_random_note = 60
        # Полифонический арпеджио: несколько зажатых клавиш образуют один аккорд,
        # по которому арпеджиатор циклит. _arp_notes: chip_idx -> raw_note.
        self._arp_notes = {}
        self._arp_playing = None   # {"seq": [...], "idx": int, "next_time": float,
                                   #  "velocity": int, "current": int | None}
        self._latched = set()   # chip_idx, latch-защёлки (не гаснут по отпусканию)

    @property
    def mode(self):
        return state.runtime["mode"]

    def set_mode(self, new_mode):
        if new_mode not in _MODES:
            return False
        self.all_notes_off()
        self._held_notes.clear()
        old = self.mode
        self.sm.set_mode(new_mode)

        if new_mode == "random_pattern":
            self._generate_random_pattern()
            if not self.sm.playing:
                self.seq.start()
        elif new_mode == "random_note":
            if self.sm.playing:
                self.seq.stop()
        elif new_mode == "midi_keyboard":
            if self.sm.playing and old in ("random_pattern", "pattern"):
                self.seq.stop()
        elif new_mode == "midi_filter":
            if self.sm.playing and old in ("random_pattern", "pattern"):
                self.seq.stop()
        return True

    # --- arpeggiator helpers ---

    def _arp_enabled(self):
        return (self.sm.pattern["arp_cfg"]["enabled"]
                and self.mode in _ARP_MODES)

    def _latch_enabled(self):
        return self.sm.pattern["arp_cfg"].get("latch", False)

    def _arp_interval_ms(self):
        ac = self.sm.pattern["arp_cfg"]
        rate = ac.get("rate", {"unit": "note", "value": "1/8"})
        bpm = self.sm.pattern["bpm"]
        if rate.get("unit") == "ms":
            try:
                ms = int(rate.get("value", 100))
            except (ValueError, TypeError):
                ms = 100
            return max(20, ms)
        div = rate.get("value", "1/8")
        beats = _NOTE_DIV_BEATS.get(div, 0.25)
        return max(20, (60.0 / bpm) * 1000.0 * beats)

    def _arp_note_set(self, raw_note):
        return build_note_set(raw_note, self.sm.pattern)

    def _build_arp_seq(self):
        # Объединяем аккорды всех зажатых клавиш (Key Filter + Chord) в один набор.
        merged = []
        seen = set()
        for raw in self._arp_notes.values():
            for n in self._arp_note_set(raw):
                if n not in seen:
                    seen.add(n)
                    merged.append(n)
        if not merged:
            return None
        merged.sort()
        ac = self.sm.pattern["arp_cfg"]
        arp_notes = apply_arp_range(merged, ac.get("range_semitones", 12), 0)
        return generate_arp_cycle(arp_notes, ac["style"], ac.get("num_steps", 8))

    def _stop_arp(self):
        if self._arp_playing is not None and self._arp_playing["current"] is not None:
            self.midi.note_off(self._arp_playing["current"])
        self._arp_playing = None

    def _restart_arp(self, now=None):
        seq = self._build_arp_seq()
        if seq is None:
            self._stop_arp()
            return
        if self._arp_playing is not None and self._arp_playing["current"] is not None:
            self.midi.note_off(self._arp_playing["current"])
        self._arp_playing = {
            "seq": seq,
            "idx": 0,
            "next_time": 0.0,
            "velocity": 100,
            "current": None,
        }
        self._advance_arp(now)

    def _advance_arp(self, now=None):
        st = self._arp_playing
        if st is None:
            return
        if now is None:
            now = time.monotonic()
        if now < st["next_time"]:
            return
        if st["current"] is not None:
            self.midi.note_off(st["current"])
        note = st["seq"][st["idx"] % len(st["seq"])]
        st["idx"] += 1
        st["current"] = note
        self.midi.note_on(note, st["velocity"])
        st["next_time"] = now + self._arp_interval_ms() / 1000.0

    def tick(self, now=None):
        self._check_config()
        if self._arp_playing is None:
            return
        if now is None:
            now = time.monotonic()
        self._advance_arp(now)

    def _check_config(self):
        # Арпеджиатор выключен в меню или режим не арпеджио — гасим арп.
        if self._arp_playing is not None and not self._arp_enabled():
            self._stop_arp()
            self._arp_notes.clear()
        # Latch выключен в меню — отпускаем защёлкнутые клавиши/арп.
        if self._latched and not self._latch_enabled():
            for chip_idx in list(self._latched):
                self._latched.discard(chip_idx)
                if self._arp_enabled():
                    self._arp_notes.pop(chip_idx, None)
                else:
                    self._release_chip(chip_idx)
            if self._arp_enabled():
                if self._arp_notes:
                    self._restart_arp()
                else:
                    self._stop_arp()

    # --- note handling ---

    def note_on(self, chip_idx, note, velocity=100):
        mode = self.mode
        if mode in ("pattern", "random_pattern"):
            return

        raw_note = note
        if mode == "random_note":
            raw_note = self._pick_random_note()

        latch = self._latch_enabled()

        # Latch: повторное нажатие защёлкнутой клавиши — отпустить.
        if latch and chip_idx in self._latched:
            self._latched.discard(chip_idx)
            if self._arp_enabled():
                self._arp_notes.pop(chip_idx, None)
                if self._arp_notes:
                    self._restart_arp()
                else:
                    self._stop_arp()
            else:
                self._release_chip(chip_idx)
            return

        if self._arp_enabled():
            if latch:
                # Mono-latch: новая клавиша ЗАМЕНЯЕТ ранее защёлкнутую (арп на новой).
                for old_chip in list(self._latched):
                    if old_chip != chip_idx:
                        self._latched.discard(old_chip)
                        self._arp_notes.pop(old_chip, None)
                self._arp_notes[chip_idx] = raw_note
                self._latched.add(chip_idx)
                self._restart_arp()
            else:
                self._arp_notes[chip_idx] = raw_note
                self._restart_arp()
        else:
            events = process_note(raw_note, velocity, self.sm.pattern, {}, live=True)
            if not events:
                return
            self._held_notes[chip_idx] = events
            for ev in events:
                self.midi.note_on(ev["note"], ev["velocity"])
            if latch:
                self._latched.add(chip_idx)

        if self.sm.recording and mode in ("midi_keyboard", "random_note", "midi_filter"):
            step = self.sm.get_step(self.sm.current_step)
            if step is not None and step["active"] and not step.get("_rec_used"):
                step["notes"] = [raw_note]
                step["_rec_used"] = True

    def note_off(self, chip_idx, note):
        # Latch: отпускание клавиши не гасит защёлкнутую ноту/арпеджио.
        if chip_idx in self._latched:
            return
        if self._arp_enabled() and chip_idx in self._arp_notes:
            self._arp_notes.pop(chip_idx, None)
            if self._arp_notes:
                self._restart_arp()
            else:
                self._stop_arp()
            return
        self._release_chip(chip_idx)

    def _release_chip(self, chip_idx):
        self._latched.discard(chip_idx)
        self._arp_notes.pop(chip_idx, None)
        if chip_idx in self._held_notes:
            for ev in self._held_notes[chip_idx]:
                self.midi.note_off(ev["note"])
            del self._held_notes[chip_idx]

    def all_notes_off(self):
        self._stop_arp()
        self._arp_notes.clear()
        self._latched.clear()
        for events in self._held_notes.values():
            for ev in events:
                self.midi.note_off(ev["note"])
        self._held_notes.clear()
        self.seq.all_notes_off()

    def _generate_random_pattern(self):
        p = self.sm.pattern
        rc = p["random_cfg"]
        for i in range(p["length"]):
            step = state.make_step()
            if random.random() < rc["density_or_probability"]:
                note = self._pick_random_note()
                step["notes"] = [note]
                step["active"] = True
                if rc["note_length"]["mode"] == "range":
                    mn = rc["note_length"]["min"]
                    mx = rc["note_length"]["max"]
                    step["length_steps"] = random.randint(mn, mx)
            p["steps"][i] = step
        self.sm.set_current_step(0)

    def _pick_random_note(self):
        rc = self.sm.pattern["random_cfg"]
        nr = rc["note_range"]
        if nr["mode"] == "simple":
            spread = {"close": 5, "medium": 12, "far": 24}
            s = spread.get(nr.get("proximity", "medium"), 12)
            self._last_random_note += random.randint(-s, s)
            self._last_random_note = max(0, min(127, self._last_random_note))
        else:
            self._last_random_note = random.randint(nr["manual_min"], nr["manual_max"])
        rc["velocity_mode"]
        return self._last_random_note
