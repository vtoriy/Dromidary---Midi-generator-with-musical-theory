import usb_midi
import adafruit_midi
from adafruit_midi.note_on import NoteOn
from adafruit_midi.note_off import NoteOff
from adafruit_midi.start import Start
from adafruit_midi.stop import Stop
from adafruit_midi.midi_continue import Continue


class MidiOut:
    def __init__(self, midi):
        self._midi = midi

    def note_on(self, note, velocity=100):
        self._midi.send(NoteOn(note, velocity))

    def note_off(self, note, velocity=0):
        self._midi.send(NoteOff(note, velocity))

    def transport_start(self):
        self._midi.send(Start())

    def transport_stop(self):
        self._midi.send(Stop())

    def transport_continue(self):
        self._midi.send(Continue())


def init_midi():
    return MidiOut(adafruit_midi.MIDI(
        midi_out=usb_midi.ports[1],
        midi_in=usb_midi.ports[0],
        out_channel=0,
    ))


def note_on(midi, note, velocity=100):
    midi.send(NoteOn(note, velocity))


def note_off(midi, note, velocity=0):
    midi.send(NoteOff(note, velocity))
