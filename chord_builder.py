from key_filter import key_filter

CHORD_TYPES = {
    "major":       [0, 4, 7],
    "minor":       [0, 3, 7],
    "diminished":  [0, 3, 6],
    "augmented":   [0, 4, 8],
    "maj7":        [0, 4, 7, 11],
    "min7":        [0, 3, 7, 10],
    "dom7":        [0, 4, 7, 10],
    "min7b5":      [0, 3, 6, 10],
    "dim7":        [0, 3, 6, 9],
    "sus2":        [0, 2, 7],
    "sus4":        [0, 5, 7],
    "chord_9":     [0, 4, 7, 10, 14],
    "chord_11":    [0, 4, 7, 10, 14, 17],
    "chord_13":    [0, 4, 7, 10, 14, 17, 21],
    "maj9":        [0, 4, 7, 11, 14],
    "7sh5":        [0, 4, 8, 10],
    "7sh9":        [0, 4, 7, 10, 15],
    "7b9":         [0, 4, 7, 10, 13],
    "7sh11":       [0, 4, 7, 10, 18],
    "7sus4":       [0, 5, 7, 10],
    "sus2_7":      [0, 2, 7, 10],
    "quartal":     [0, 5, 10],
    "quintal":     [0, 7, 14],
    "cluster":     [0, 1, 2, 3],
    "power":       [0, 7],
}


def build_chord(root_note, chord_type, bass_note=None):
    intervals = CHORD_TYPES.get(chord_type)
    if intervals is None:
        return [root_note]

    notes = [root_note + i for i in intervals]
    notes = [n for n in notes if 0 <= n <= 127]

    if bass_note is not None and chord_type == "slash":
        notes.insert(0, bass_note)

    return notes


def secondary_filter(notes, key_filter_cfg):
    if not key_filter_cfg["enabled"]:
        return notes

    filtered = []
    for n in notes:
        snapped = key_filter(n, key_filter_cfg)
        if snapped is not None:
            filtered.append(snapped)
    return filtered


def get_voiced_events(notes, voicing, strum_delay_ms):
    if not notes:
        return []

    if voicing == "block":
        return [(n, 0) for n in notes]

    if voicing == "strum":
        return [(n, i * strum_delay_ms) for i, n in enumerate(notes)]

    if voicing == "roll":
        events = []
        for i, n in enumerate(notes):
            events.append((n, i * strum_delay_ms))
        return events

    return [(n, 0) for n in notes]
