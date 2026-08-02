SCALES = {
    "major":           [0, 2, 4, 5, 7, 9, 11],
    "minor":           [0, 2, 3, 5, 7, 8, 10],
    "dorian":          [0, 2, 3, 5, 7, 9, 10],
    "phrygian":        [0, 1, 3, 5, 7, 8, 10],
    "lydian":          [0, 2, 4, 6, 7, 9, 11],
    "mixolydian":      [0, 2, 4, 5, 7, 9, 10],
    "locrian":         [0, 1, 3, 5, 6, 8, 10],
    "harmonic_minor":  [0, 2, 3, 5, 7, 8, 11],
    "melodic_minor":   [0, 2, 3, 5, 7, 9, 11],
    "pentatonic_major": [0, 2, 4, 7, 9],
    "pentatonic_minor": [0, 3, 5, 7, 10],
    "blues":           [0, 3, 5, 6, 7, 10],
    "whole_tone":      [0, 2, 4, 6, 8, 10],
    "diminished":      [0, 2, 3, 5, 6, 8, 9, 11],
    "chromatic":       [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11],
}


def build_scale_notes(root_note, scale_name):
    intervals = SCALES.get(scale_name)
    if intervals is None:
        return None
    root_class = root_note % 12
    notes = set()
    for octave in range(-1, 10):
        base = octave * 12
        for interval in intervals:
            midi = base + root_class + interval
            if 0 <= midi <= 127:
                notes.add(midi)
    return notes


def snap_note_up(note, scale_notes):
    if note in scale_notes:
        return note
    for n in range(note + 1, 128):
        if n in scale_notes:
            return n
    return None


def snap_note_down(note, scale_notes):
    if note in scale_notes:
        return note
    for n in range(note - 1, -1, -1):
        if n in scale_notes:
            return n
    return None


def key_filter(note, cfg):
    if not cfg["enabled"]:
        return note

    root = cfg["root_note"]
    scale_name = cfg["scale"]
    mode = cfg["mode"]

    scale_notes = build_scale_notes(root, scale_name)
    if scale_notes is None:
        return note

    if note in scale_notes:
        return note

    if mode == "snap_up":
        return snap_note_up(note, scale_notes)
    elif mode == "snap_down":
        return snap_note_down(note, scale_notes)
    elif mode == "mute":
        return None

    return note
