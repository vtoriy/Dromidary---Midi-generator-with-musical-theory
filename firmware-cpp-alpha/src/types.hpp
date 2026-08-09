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
    UpDown,
    DownUp,
    AsPlayed,
    Random,
    ConvergeDiverge,
    kCount,
};

enum class RateMode : uint8_t {
    Note = 0,
    Ms,
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
    uint8_t length_steps {4};
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
    uint8_t rate_note_index {2};  // index into kArpNoteDivs => "1/8"
    uint16_t rate_ms {100};       // 10..2000 step 10
    uint8_t range_semitones {12};
    uint8_t num_steps {8};
    ArpStyle style {ArpStyle::Up};
};

struct TimingCfg {
    uint8_t swing_pct {0};
    uint8_t humanize_ms {0};
    uint8_t quantize_grid {0};    // index into kQuantizeGrids
    bool legato {false};
};

struct GateCfg {
    bool enabled {false};
    uint16_t attack_ms {0};
    uint16_t decay_ms {0};
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
    uint8_t shape {0};                    // index into kShapeOptions
};

// Persistent input-click timing settings (survive device reboot).
struct ClickSettings {
    uint16_t debounce_ms {12};    // single-click bounce filter (func keys)
    uint16_t double_ms {300};     // double-click window (DETAIL/MAIN reset)
    uint16_t long_ms {800};       // long-press threshold (joystick)
};

struct Pattern {
    std::array<Step, kStepCountMax> steps {};
    uint8_t length {16};
    uint16_t bpm {120};
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
    uint8_t base_octave {2};      // recognised octave of the note keys
    bool live_mute {false};       // Rest held during playback
    uint8_t last_note {0};        // last/live note shown in the status bar
    bool show_note {false};
    uint8_t last_input_note {0};  // RandomNote: last pressed key note
    uint8_t func_bits {0};        // latest raw chip3 image: bit i = raw bit 16+i, 1 = pressed
    uint16_t note_bits {0};       // latest raw chip1+2 image: bit i = raw bit i, 1 = pressed
    ClickSettings click {};
    bool test_mode {false};       // FULL menu "Test" screen active
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
};

// ---------------------------------------------------------------------------
// Quick (Level 1) cell model
// ---------------------------------------------------------------------------

enum class SegmentType : uint8_t {
    Linear = 0,   // click to edit, then left/right
    Radial,       // click to edit, then joystick direction -> zone
    Toggle,       // click confirms an immediate 0/1 flip (no edit mode)
    Param,        // click -> DETAIL submenu
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
