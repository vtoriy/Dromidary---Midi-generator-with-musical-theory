import random


def generate_arp_cycle(notes, style, num_steps):
    if not notes:
        return []

    if len(notes) == 1:
        return [notes[0]] * num_steps

    cycle = []

    if style == "up":
        for i in range(num_steps):
            cycle.append(notes[i % len(notes)])

    elif style == "down":
        for i in range(num_steps):
            cycle.append(notes[-(i % len(notes)) - 1])

    elif style == "up_down":
        pattern = notes + notes[-2:0:-1]
        for i in range(num_steps):
            cycle.append(pattern[i % len(pattern)])

    elif style == "down_up":
        rev = list(reversed(notes))
        pattern = rev + rev[-2:0:-1]
        for i in range(num_steps):
            cycle.append(pattern[i % len(pattern)])

    elif style == "as_played":
        for i in range(num_steps):
            cycle.append(notes[i % len(notes)])

    elif style == "random":
        for _ in range(num_steps):
            cycle.append(random.choice(notes))

    elif style == "converge_diverge":
        left = 0
        right = len(notes) - 1
        pattern = []
        while left <= right:
            if left == right:
                pattern.append(notes[left])
            else:
                pattern.append(notes[left])
                pattern.append(notes[right])
            left += 1
            right -= 1
        diverge = list(reversed(pattern))
        full_pattern = pattern + diverge[1:-1] if len(diverge) > 2 else pattern
        for i in range(num_steps):
            cycle.append(full_pattern[i % len(full_pattern)])

    else:
        for i in range(num_steps):
            cycle.append(notes[i % len(notes)])

    return cycle


def apply_arp_range(notes, range_semitones, num_steps):
    if range_semitones == 0:
        return notes

    extended = list(notes)
    for octave in range(1, (range_semitones // 12) + 2):
        for n in notes:
            shifted = n + octave * 12
            if shifted <= 127 and (shifted - min(notes)) <= range_semitones:
                extended.append(shifted)

    return extended[:num_steps] if num_steps > 0 else extended
