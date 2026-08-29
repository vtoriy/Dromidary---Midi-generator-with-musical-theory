#pragma once

#include <array>
#include <cstdint>
#include <functional>

namespace drom {

// ---------------------------------------------------------------------------
// Fixed sizing constants (static memory, no dynamic allocation on hot paths)
// ---------------------------------------------------------------------------

constexpr std::size_t kStepCountMax = 64;
constexpr std::size_t kSlotCount = 16;
constexpr std::size_t kMaxChordNotes = 8;
constexpr std::size_t kMaxArpNotes = 64;
constexpr std::size_t kMaxHeldKeys = 16;
constexpr std::size_t kKeyCount = 16;

// Editable note range for RandomNote (MIDI note numbers): C0..B8.
constexpr int32_t kNoteRangeMin = 12;
constexpr int32_t kNoteRangeMax = 119;

// ---------------------------------------------------------------------------
// Enumerations (mirror the Python key_filter/chord_builder/arpeggiator lists)
// ---------------------------------------------------------------------------

enum class PlayMode : uint8_t {
    MidiKeyboard = 0,
    Pattern,
    RandomPattern,
    RandomNote,
    MidiFilter,
};

enum class ScreenMode : uint8_t {
    Quick = 0,
    Full,
    Edit,       // pattern editor (grid view + step detail)
    Animation,
};

enum class ScaleId : uint8_t {
    Off = 0,
    Major,
    Minor,
    Dorian,
    Phrygian,
    Lydian,
    Mixolydian,
    Locrian,
    HarmonicMinor,
    MelodicMinor,
    PentatonicMajor,
    PentatonicMinor,
    Blues,
    WholeTone,
    Diminished,
    Chromatic,
    kCount,
};

enum class SnapMode : uint8_t {
    SnapUp = 0,
    SnapDown,
    Mute,
    kCount,
};

enum class ChordType : uint8_t {
    Off = 0,
    Major,
    Minor,
    Diminished,
    Augmented,
    Maj7,
    Min7,
    Dom7,
    Min7b5,
    Dim7,
    Chord9,
    Chord11,
    Chord13,
    Maj9,
    S7sh5,    // 7#5
    S7sh9,    // 7#9
    S7b9,     // 7b9
    S7sh11,   // 7#11
    Sus2,
    Sus4,
    S7sus4,   // 7sus4
    Sus2_7,   // sus2/7
    Quartal,
    Quintal,
    Cluster,
    Power,
    kCount,
};

enum class VoicingMode : uint8_t {
    Block = 0,
    Strum,
    Roll,
    kCount,
};

enum class ArpStyle : uint8_t {
    Off = 0,
    Up,
    Down,
    UpDown,          // волна вверх-вниз, крайние ноты по разу
    DownUp,          // то же, от верхней к нижней
    UpDownRep,       // Up & Down: волна, крайние ноты повторяются на развороте
    DownUpRep,       // Down & Up: зеркально
    Converge,        // с внешних нот к центру аккорда
    Diverge,         // из центра к крайним
    ConvergeDiverge, // Con&Diverge: оба хода в одном цикле
    PinkyUp,         // верхняя нота = педаль, остальные восходят между её нотами
    PinkyUpDown,     // педаль-верх + остальные волной вверх-вниз
    ThumbUp,         // нижняя нота = остинато-бас, остальные восходят поверх
    ThumbUpDown,     // бас-педаль + остальные волной
    AsPlayed,        // Play Order: в порядке нажатия клавиш
    ChordTrigger,    // аккорд целиком повторяется на каждом шаге (ритм-гейт)
    Random,          // непрерывно случайная последовательность
    RandomOnce,      // один случайный паттерн, закреплён на время аккорда
    RandomOther,     // случайный порядок без повторов внутри цикла
    kCount,
};

enum class RateMode : uint8_t {
    Note = 0,
    Ms,
    kCount,
};

enum class ClockSync : uint8_t {
    Off = 0,
    Master,
    Slave,
    kCount,
};

// ---------------------------------------------------------------------------
// Pattern configuration structs
// ---------------------------------------------------------------------------

struct Step {
    std::array<uint8_t, 4> notes {};
    uint8_t note_count {0};
    bool active {false};
    bool tie {false};
    // Gate length as an index into kNoteLenDivs (1/128..'4). Onsets live on
    // the pattern grid (1/16 or 1/64 by Pattern.grid64); the duration itself
    // is division-exact, so 1/64 notes survive recording.
    uint8_t len_div {8};
};

struct KeyFilterCfg {
    bool enabled {false};
    uint8_t root_note {0};        // 0..11 (C..B)
    ScaleId scale {ScaleId::Off};
    SnapMode mode {SnapMode::SnapUp};
};

struct ChordCfg {
    bool enabled {false};
    ChordType type {ChordType::Major};
    VoicingMode voicing {VoicingMode::Block};
    uint8_t strum_delay_ms {10};
};

struct ArpCfg {
    bool enabled {false};
    bool latch {false};
    RateMode rate_mode {RateMode::Note};
    uint8_t rate_note_index {6};      // индекс в kArpNoteDivs => "1/8" (доли такта)
    uint16_t rate_ms {100};           // Rate (Free): 10..2000 мс, шаг 10
    uint8_t distance_semitones {12};  // Distance: интервал транспонирования (полутоны, 0..48; 12 = октава)
    uint8_t steps {1};                // Steps: сколько доп. транспозиций (позиций = steps+1; 0 = только исходная высота)
    uint8_t cycle {8};                // Cycle: длина цикла арпеджио в шагах (1..32)
    ArpStyle style {ArpStyle::Up};
};

struct TimingCfg {
    uint16_t bpm {120};           // темп, 20..300
    uint8_t swing_pct {0};        // 0..100
    uint8_t humanize_ms {0};      // 0..50
    uint8_t quantize_grid {0};    // index into kQuantizeGrids
    bool legato {false};
    ClockSync clock {ClockSync::Off};  // MIDI Clock: Off/Master/Slave
};

struct GateCfg {
    bool enabled {false};
    uint16_t attack_ms {0};
    uint16_t decay_ms {500};
    uint8_t sustain_pct {100};
    uint16_t release_ms {0};
    bool sync_quantize {false};   // true = "quantize" sync (gate_cfg.sync)
};

struct TransposeCfg {
    int8_t semitones {0};
    int8_t octaves {0};
};

struct RandomCfg {
    uint8_t density_or_probability {50};  // 0..100
    uint8_t shape {0};                    // index into kShapeOptions (reserved)
    uint8_t note_min {24};                // lower bound of the random note range (MIDI, C1 default)
    uint8_t note_max {35};                // upper bound of the random note range (MIDI, B1 default)
    uint8_t len_min_idx {8};              // LEN lower bound (kNoteLenDivs, "8" = 1/8)
    uint8_t len_max_idx {8};              // LEN upper bound (kNoteLenDivs, "8" = 1/8)
    uint8_t repeat {0};                   // anchor-repeat chance in tens of %: 0..9 => 0..90 %
    bool len_chain {true};                // true = chained lengths (next onset follows the
                                          // gate end); false = ARP Rate grid, length capped
    bool len_triplets {false};            // include triplet divisions in the LEN list
    uint8_t gate_pct {100};               // gate as % of the event length (20..100, step 10);
                                          // 100 = legato chain, less = articulation gap
};

// Persistent input-click timing settings (survive device reboot).
struct ClickSettings {
    uint16_t debounce_ms {12};    // single-click bounce filter (func keys)
    uint16_t double_ms {300};     // double-click window (DETAIL/MAIN reset)
    uint16_t long_ms {800};       // long-press threshold (joystick)
    uint32_t idle_ms {10000};     // idle before screensaver animation (ms)
};

struct Pattern {
    std::array<Step, kStepCountMax> steps {};
    uint8_t length {16};          // loop length in grid steps (STEP cell: 4..64)
    bool grid64 {false};          // false = onset grid 1/16 note, true = 1/64 note
    KeyFilterCfg key_filter {};
    ChordCfg chord {};
    ArpCfg arp {};
    TimingCfg timing {};
    GateCfg gate {};
    TransposeCfg transpose {};
    RandomCfg random {};
};

struct RuntimeState {
    PlayMode mode {PlayMode::MidiKeyboard};
    ScreenMode screen_mode {ScreenMode::Quick};
    bool playing {false};
    bool recording {false};
    uint8_t current_step {0};
    uint8_t beat {0};             // 0..3 running quarter of the transport beat
    uint8_t base_octave {2};      // recognised octave of the note keys
    bool live_mute {false};       // Rest held during playback
    uint8_t last_note {0};        // last/live note shown in the status bar
    bool show_note {false};
    uint8_t last_input_note {0};  // RandomNote: last pressed key note
    uint8_t func_bits {0};        // latest raw chip3 image: bit i = raw bit 16+i, 1 = pressed
    uint16_t note_bits {0};       // latest raw chip1+2 image: bit i = raw bit i, 1 = pressed
    ClickSettings click {};
    bool test_mode {false};       // FULL menu "Test" screen active
    bool regen_req {false};       // Randomize -> Regen: re-roll the PTRN slot
};

struct NoteSet {
    std::array<uint8_t, kMaxArpNotes> notes {};
    uint8_t count {0};
};

// ---------------------------------------------------------------------------
// Menu model (built at runtime against a Pattern, values edited live)
// ---------------------------------------------------------------------------

enum class MenuItemType : uint8_t {
    Section = 0,   // enters children on click
    Group,         // summary + children on click
    Toggle,        // 0/1, displayed On/Off
    Option,        // index into option_labels / dynamic label fn
    IntSlider,     // int range min..max
    Rate,          // arp rate: note divisions (mode==Note) or ms int (mode==Ms)
    NoteRange,     // min..max MIDI notes, 2D joystick editing (DETAIL/FULL)
    Action,        // runs a callback on click
};

using IntGetter = std::function<int32_t()>;
using IntSetter = std::function<void(int32_t)>;

struct MenuItem;

struct MenuItem {
    MenuItemType type {MenuItemType::Section};
    const char* label {nullptr};
    const MenuItem* children {nullptr};
    int child_count {0};
    IntGetter get_i;
    IntSetter set_i;
    int32_t min_v {0};
    int32_t max_v {0};
    int32_t step {1};
    const char* const* option_labels {nullptr};
    int option_count {0};
    std::function<const char*(int32_t)> label_fn;
    std::function<int32_t()> unit_get;   // Rate: 0 = Note divisions, 1 = ms
    std::function<void()> action;        // Action items: run on click

    // NoteRange: two independent bounds with live get/set. min_v/max_v hold
    // the editable MIDI-note span (e.g. 12..119 = C0..B8).
    IntGetter get_min;
    IntSetter set_min;
    IntGetter get_max;
    IntSetter set_max;
};

// ---------------------------------------------------------------------------
// Quick (Level 1) cell model
// ---------------------------------------------------------------------------

enum class SegmentType : uint8_t {
    Linear = 0,   // click to edit, then left/right
    Radial,       // click to edit, then joystick direction -> zone
    Toggle,       // click confirms an immediate 0/1 flip (no edit mode)
    Param,        // click -> DETAIL submenu
    Range,        // min..max note span: up/down = max, left/right = centre
};

struct Segment {
    SegmentType type {SegmentType::Linear};
    const char* label {nullptr};
    IntGetter get;
    IntSetter set;
    const char* const* labels {nullptr};
    int count {0};
    const MenuItem* children {nullptr};
    int child_count {0};
    bool direct {false};   // left/right edits immediately (no click-first)
    std::function<const char*(int32_t)> label_fn;  // dynamic cell label (overrides labels)

    // Range segments: two independent bounds with live get/set. get/set stay
    // nullptr for Range; the renderer formats the span from these accessors.
    IntGetter get_min;
    IntSetter set_min;
    IntGetter get_max;
    IntSetter set_max;
    // Editable bounds for the tilt editor (clamps of min/max). Defaults target
    // MIDI notes; length-range cells override them with their own index span.
    int32_t bound_lo {kNoteRangeMin};
    int32_t bound_hi {kNoteRangeMax};
};

constexpr int kMaxSegsPerRow = 3;

struct QuickRow {
    const char* label {nullptr};
    std::array<Segment, kMaxSegsPerRow> segments {};
    int seg_count {0};
    const MenuItem* submenu {nullptr};
    int submenu_count {0};
    std::function<const char*()> summary_fn;
};

}  // namespace drom
