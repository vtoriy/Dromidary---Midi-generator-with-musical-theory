#include "menu_items.hpp"

#include "../engine/midi_chain.hpp"

namespace drom {

// ---------------------------------------------------------------------------
// Static label tables (mirror menu.py)
// ---------------------------------------------------------------------------

const char* const kNoteNames[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

const char* const kScaleRadialLabels[8] = {"Off", "Maj", "Min", "Dor", "Phr", "Lyd", "Mix", "Blu"};
const char* const kCTypeRadialLabels[8] = {"Off", "Maj", "Min", "Maj7", "Min7", "7", "Sus4", "Pow"};
const char* const kAStyleRadialLabels[8] = {"Off", "Up", "Down", "UpDn", "DnUp", "Play", "Rnd", "CvDv"};
const char* const kStrumLabels[8] = {"Off", "10", "20", "30", "40", "50", "75", "100"};
constexpr int kStrumZones[8] = {0, 10, 20, 30, 40, 50, 75, 100};
const char* const kSwingRadialLabels[8] = {"Off", "12", "25", "33", "42", "50", "62", "75"};
constexpr int kSwingZones[8] = {0, 12, 25, 33, 42, 50, 62, 75};
const char* const kHumRadialLabels[8] = {"0", "5", "10", "15", "20", "25", "30", "40"};
constexpr int kHumZones[8] = {0, 5, 10, 15, 20, 25, 30, 40};

const char* const kScaleFullLabels[16] = {
    "Off", "Maj", "Min", "Dor", "Phr", "Lyd", "Mix", "Loc",
    "HMin", "MMin", "PMaj", "PMin", "Blu", "WhT", "Dim", "Chr",
};
const ScaleId kScaleFullIds[16] = {
    ScaleId::Off, ScaleId::Major, ScaleId::Minor, ScaleId::Dorian, ScaleId::Phrygian,
    ScaleId::Lydian, ScaleId::Mixolydian, ScaleId::Locrian, ScaleId::HarmonicMinor,
    ScaleId::MelodicMinor, ScaleId::PentatonicMajor, ScaleId::PentatonicMinor,
    ScaleId::Blues, ScaleId::WholeTone, ScaleId::Diminished, ScaleId::Chromatic,
};
constexpr int kScaleFullCount = 16;

const char* const kCTypeFullLabels[27] = {
    "Off", "Maj", "Min", "Dim", "Aug", "Maj7", "Min7", "7", "m7b5", "Dim7",
    "9", "11", "13", "Maj9", "7#5", "7#9", "7b9", "7#11",
    "Sus2", "Sus4", "7s4", "s2/7", "Qrt", "Qnt", "Cls", "Pow",
};
const ChordType kCTypeFullIds[27] = {
    ChordType::Off, ChordType::Major, ChordType::Minor, ChordType::Diminished,
    ChordType::Augmented, ChordType::Maj7, ChordType::Min7, ChordType::Dom7,
    ChordType::Min7b5, ChordType::Dim7, ChordType::Chord9, ChordType::Chord11,
    ChordType::Chord13, ChordType::Maj9, ChordType::S7sh5, ChordType::S7sh9,
    ChordType::S7b9, ChordType::S7sh11, ChordType::Sus2, ChordType::Sus4,
    ChordType::S7sus4, ChordType::Sus2_7, ChordType::Quartal, ChordType::Quintal,
    ChordType::Cluster, ChordType::Power,
};
constexpr int kCTypeFullCount = 26;

const char* const kAStyleFullLabels[19] = {
    "Off", "Up", "Down", "UpDn", "DnUp", "Up&Dn", "Dn&Up",
    "Converge", "Diverge", "C&Div",
    "PinkUp", "PinkDn", "ThmbUp", "ThmbUD",
    "PlayOrd", "Chord", "Rnd", "Rnd1", "RndO",
};
const ArpStyle kAStyleFullIds[19] = {
    ArpStyle::Off, ArpStyle::Up, ArpStyle::Down, ArpStyle::UpDown,
    ArpStyle::DownUp, ArpStyle::UpDownRep, ArpStyle::DownUpRep,
    ArpStyle::Converge, ArpStyle::Diverge, ArpStyle::ConvergeDiverge,
    ArpStyle::PinkyUp, ArpStyle::PinkyUpDown, ArpStyle::ThumbUp, ArpStyle::ThumbUpDown,
    ArpStyle::AsPlayed, ArpStyle::ChordTrigger, ArpStyle::Random,
    ArpStyle::RandomOnce, ArpStyle::RandomOther,
};
constexpr int kAStyleFullCount = 19;

const char* const kVoicingLabels[3] = {"Blk", "Strm", "Roll"};
const VoicingMode kVoicingIds[3] = {VoicingMode::Block, VoicingMode::Strum, VoicingMode::Roll};

const char* const kSnapLabels[3] = {"Up", "Dn", "Mute"};
const SnapMode kSnapIds[3] = {SnapMode::SnapUp, SnapMode::SnapDown, SnapMode::Mute};

const char* const kOnOffLabels[2] = {"Off", "On"};
const char* const kModeLabels[2] = {"KB", "RND"};
const char* const kRateModeLabels[2] = {"Note", "Free"};
const RateMode kRateModeIds[2] = {RateMode::Note, RateMode::Ms};

const char* const kLengthLabels[4] = {"16", "32", "48", "64"};
const char* const kQuantizeLabels[8] = {"Off", "1/32", "1/16T", "1/16", "1/8T", "1/8", "1/4T", "1/4"};
const char* const kShapeLabels[4] = {"Asc", "Desc", "Arch", "Rnd"};

namespace {

// ---------------------------------------------------------------------------
// Pool helpers
// ---------------------------------------------------------------------------

struct ItemRef {
    const MenuItem* items {nullptr};
    int count {0};
};

// Forward declarations (definitions below the block builders).
int scale_full_index(const KeyFilterCfg& kf);
void apply_scale_full(KeyFilterCfg& kf, int idx);
int ctype_full_index(const ChordCfg& cc);
void apply_ctype_full(ChordCfg& cc, int idx);
int astyle_full_index(const ArpCfg& ac);
void apply_astyle_full(ArpCfg& ac, int idx);

MenuItem& emit(MenuContent& c, const MenuItem& m) {
    c.items[c.item_count] = m;
    return c.items[c.item_count++];
}

MenuItem& emit_section(MenuContent& c, const char* label, std::size_t start, int child_count) {
    MenuItem s;
    s.type = MenuItemType::Section;
    s.label = label;
    s.children = &c.items[start];
    s.child_count = child_count;
    return emit(c, s);
}

ItemRef wrap_ref(MenuContent& c, std::size_t start) {
    ItemRef r;
    r.items = &c.items[start];
    r.count = static_cast<int>(c.item_count - start);
    return r;
}

// ---------------------------------------------------------------------------
// Item builders
// ---------------------------------------------------------------------------

MenuItem toggle_item_io(const char* label, IntGetter get, IntSetter set) {
    MenuItem m;
    m.type = MenuItemType::Toggle;
    m.label = label;
    m.get_i = std::move(get);
    m.set_i = std::move(set);
    return m;
}

MenuItem option_idx_io(const char* label, IntGetter get, IntSetter set,
                       const char* const* labels, int count) {
    MenuItem m;
    m.type = MenuItemType::Option;
    m.label = label;
    m.get_i = std::move(get);
    m.set_i = std::move(set);
    m.option_labels = labels;
    m.option_count = count;
    return m;
}

MenuItem int_slider_io(const char* label, IntGetter get, IntSetter set,
                       int32_t min_v, int32_t max_v) {
    MenuItem m;
    m.type = MenuItemType::IntSlider;
    m.label = label;
    m.get_i = std::move(get);
    m.set_i = std::move(set);
    m.min_v = min_v;
    m.max_v = max_v;
    m.step = 1;
    return m;
}

MenuItem rate_item(const char* label, IntGetter get, IntSetter set, IntGetter unit_get) {
    MenuItem m;
    m.type = MenuItemType::Rate;
    m.label = label;
    m.get_i = std::move(get);
    m.set_i = std::move(set);
    m.unit_get = std::move(unit_get);
    m.option_count = kArpNoteDivCount;  // note-division modes
    m.min_v = 10;                       // ms mode
    m.max_v = 2000;
    m.step = 10;
    return m;
}

MenuItem action_item(const char* label, std::function<void()> fn) {
    MenuItem m;
    m.type = MenuItemType::Action;
    m.label = label;
    m.action = std::move(fn);
    return m;
}

// ---------------------------------------------------------------------------
// Shared per-block builders. Used BOTH by the QUICK `PRM` DETAIL submenu and
// by the FULL menu section, so DETAIL and FULL always expose identical content.
// ---------------------------------------------------------------------------

ItemRef emit_key_block(MenuContent& c, Pattern& p) {
    const std::size_t start = c.item_count;
    emit(c, toggle_item_io("Key Filter",
        [&p]() { return p.key_filter.enabled ? 1 : 0; },
        [&p](int32_t v) { p.key_filter.enabled = (v != 0); }));
    emit(c, option_idx_io("Root Note",
        [&p]() { return static_cast<int32_t>(p.key_filter.root_note); },
        [&p](int32_t v) { p.key_filter.root_note = static_cast<uint8_t>(v % 12); },
        kNoteNames, 12));
    emit(c, option_idx_io("Scale",
        [&p]() { return static_cast<int32_t>(scale_full_index(p.key_filter)); },
        [&p](int32_t v) { apply_scale_full(p.key_filter, static_cast<int>(v)); },
        kScaleFullLabels, kScaleFullCount));
    emit(c, option_idx_io("Snap",
        [&p]() { return static_cast<int32_t>(p.key_filter.mode); },
        [&p](int32_t v) { p.key_filter.mode = static_cast<SnapMode>(v % 3); },
        kSnapLabels, 3));
    return wrap_ref(c, start);
}

ItemRef emit_chord_block(MenuContent& c, Pattern& p) {
    const std::size_t start = c.item_count;
    emit(c, toggle_item_io("Chord",
        [&p]() { return p.chord.enabled ? 1 : 0; },
        [&p](int32_t v) { p.chord.enabled = (v != 0); }));
    emit(c, option_idx_io("Type",
        [&p]() { return static_cast<int32_t>(ctype_full_index(p.chord)); },
        [&p](int32_t v) { apply_ctype_full(p.chord, static_cast<int>(v)); },
        kCTypeFullLabels, kCTypeFullCount));
    emit(c, option_idx_io("Voicing",
        [&p]() { return static_cast<int32_t>(p.chord.voicing); },
        [&p](int32_t v) { p.chord.voicing = static_cast<VoicingMode>(v % 3); },
        kVoicingLabels, 3));
    emit(c, int_slider_io("Strum",
        [&p]() { return static_cast<int32_t>(p.chord.strum_delay_ms); },
        [&p](int32_t v) { p.chord.strum_delay_ms = static_cast<uint8_t>(v); }, 1, 150));
    return wrap_ref(c, start);
}

ItemRef emit_arp_block(MenuContent& c, Pattern& p) {
    const std::size_t start = c.item_count;
    emit(c, toggle_item_io("Arp",
        [&p]() { return p.arp.enabled ? 1 : 0; },
        [&p](int32_t v) { p.arp.enabled = (v != 0); }));
    emit(c, toggle_item_io("Latch",
        [&p]() { return p.arp.latch ? 1 : 0; },
        [&p](int32_t v) { p.arp.latch = (v != 0); }));
    emit(c, option_idx_io("Style",
        [&p]() { return static_cast<int32_t>(astyle_full_index(p.arp)); },
        [&p](int32_t v) { apply_astyle_full(p.arp, static_cast<int>(v)); },
        kAStyleFullLabels, kAStyleFullCount));
    emit(c, option_idx_io("RateMode",
        [&p]() { return static_cast<int32_t>(p.arp.rate_mode); },
        [&p](int32_t v) { p.arp.rate_mode = static_cast<RateMode>(v % 2); },
        kRateModeLabels, 2));
    emit(c, rate_item("Rate",
        [&p]() { return p.arp.rate_mode == RateMode::Note ? static_cast<int32_t>(p.arp.rate_note_index) : static_cast<int32_t>(p.arp.rate_ms); },
        [&p](int32_t v) { if (p.arp.rate_mode == RateMode::Note) { p.arp.rate_note_index = static_cast<uint8_t>(v % kArpNoteDivCount); } else { p.arp.rate_ms = static_cast<uint16_t>(v); } },
        [&p]() { return p.arp.rate_mode == RateMode::Note ? 0 : 1; }));
    emit(c, int_slider_io("Distance",
        [&p]() { return static_cast<int32_t>(p.arp.distance_semitones); },
        [&p](int32_t v) { p.arp.distance_semitones = static_cast<uint8_t>(v); }, 0, 48));
    emit(c, int_slider_io("Steps",
        [&p]() { return static_cast<int32_t>(p.arp.steps); },
        [&p](int32_t v) { p.arp.steps = static_cast<uint8_t>(v); }, 0, 16));
    emit(c, int_slider_io("Cycle",
        [&p]() { return static_cast<int32_t>(p.arp.cycle); },
        [&p](int32_t v) { p.arp.cycle = static_cast<uint8_t>(v); }, 1, 32));
    return wrap_ref(c, start);
}

ItemRef emit_timing_block(MenuContent& c, Pattern& p) {
    const std::size_t start = c.item_count;
    emit(c, int_slider_io("BPM",
        [&p]() { return static_cast<int32_t>(p.timing.bpm); },
        [&p](int32_t v) { p.timing.bpm = static_cast<uint16_t>(v); }, 20, 300));
    emit(c, int_slider_io("Swing",
        [&p]() { return static_cast<int32_t>(p.timing.swing_pct); },
        [&p](int32_t v) { p.timing.swing_pct = static_cast<uint8_t>(v); }, 0, 100));
    emit(c, int_slider_io("Humanize",
        [&p]() { return static_cast<int32_t>(p.timing.humanize_ms); },
        [&p](int32_t v) { p.timing.humanize_ms = static_cast<uint8_t>(v); }, 0, 50));
    emit(c, option_idx_io("Quantize",
        [&p]() { return static_cast<int32_t>(p.timing.quantize_grid); },
        [&p](int32_t v) { p.timing.quantize_grid = static_cast<uint8_t>(v % 8); },
        kQuantizeLabels, 8));
    emit(c, toggle_item_io("Legato",
        [&p]() { return p.timing.legato ? 1 : 0; },
        [&p](int32_t v) { p.timing.legato = (v != 0); }));
    return wrap_ref(c, start);
}

ItemRef emit_adsr_block(MenuContent& c, Pattern& p) {
    const std::size_t start = c.item_count;
    emit(c, toggle_item_io("Switch",
        [&p]() { return p.gate.enabled ? 1 : 0; },
        [&p](int32_t v) { p.gate.enabled = (v != 0); }));
    emit(c, int_slider_io("Atk",
        [&p]() { return static_cast<int32_t>(p.gate.attack_ms); },
        [&p](int32_t v) { p.gate.attack_ms = static_cast<uint16_t>(v); }, 0, 500));
    emit(c, int_slider_io("Dec",
        [&p]() { return static_cast<int32_t>(p.gate.decay_ms); },
        [&p](int32_t v) { p.gate.decay_ms = static_cast<uint16_t>(v); }, 0, 500));
    emit(c, int_slider_io("Sus",
        [&p]() { return static_cast<int32_t>(p.gate.sustain_pct); },
        [&p](int32_t v) { p.gate.sustain_pct = static_cast<uint8_t>(v); }, 0, 100));
    emit(c, int_slider_io("Rel",
        [&p]() { return static_cast<int32_t>(p.gate.release_ms); },
        [&p](int32_t v) { p.gate.release_ms = static_cast<uint16_t>(v); }, 0, 500));
    emit(c, option_idx_io("Sync",
        [&p]() { return p.gate.sync_quantize ? 1 : 0; },
        [&p](int32_t v) { p.gate.sync_quantize = (v != 0); },
        kRateModeLabels, 2));
    return wrap_ref(c, start);
}

// ---------------------------------------------------------------------------
// Radial zone -> config mapping (Quick cells)
// ---------------------------------------------------------------------------

int scale_zone(const KeyFilterCfg& kf) {
    if (!kf.enabled) { return 0; }
    switch (kf.scale) {
        case ScaleId::Major: return 1;
        case ScaleId::Minor: return 2;
        case ScaleId::Dorian: return 3;
        case ScaleId::Phrygian: return 4;
        case ScaleId::Lydian: return 5;
        case ScaleId::Mixolydian: return 6;
        case ScaleId::Blues: return 7;
        default: return 1;
    }
}

void apply_scale_zone(KeyFilterCfg& kf, int zone) {
    if (zone < 0 || zone > 7) { return; }
    if (zone == 0) {
        kf.enabled = false;
        kf.scale = ScaleId::Off;
        return;
    }
    kf.enabled = true;
    switch (zone) {
        case 1: kf.scale = ScaleId::Major; break;
        case 2: kf.scale = ScaleId::Minor; break;
        case 3: kf.scale = ScaleId::Dorian; break;
        case 4: kf.scale = ScaleId::Phrygian; break;
        case 5: kf.scale = ScaleId::Lydian; break;
        case 6: kf.scale = ScaleId::Mixolydian; break;
        case 7: kf.scale = ScaleId::Blues; break;
    }
}

int ctype_zone(const ChordCfg& cc) {
    if (!cc.enabled) { return 0; }
    switch (cc.type) {
        case ChordType::Major: return 1;
        case ChordType::Minor: return 2;
        case ChordType::Maj7: return 3;
        case ChordType::Min7: return 4;
        case ChordType::Dom7: return 5;
        case ChordType::Sus4: return 6;
        case ChordType::Power: return 7;
        default: return 1;
    }
}

void apply_ctype_zone(ChordCfg& cc, int zone) {
    if (zone < 0 || zone > 7) { return; }
    if (zone == 0) {
        cc.enabled = false;
        return;
    }
    cc.enabled = true;
    switch (zone) {
        case 1: cc.type = ChordType::Major; break;
        case 2: cc.type = ChordType::Minor; break;
        case 3: cc.type = ChordType::Maj7; break;
        case 4: cc.type = ChordType::Min7; break;
        case 5: cc.type = ChordType::Dom7; break;
        case 6: cc.type = ChordType::Sus4; break;
        case 7: cc.type = ChordType::Power; break;
    }
}

int astyle_zone(const ArpCfg& ac) {
    if (!ac.enabled) { return 0; }
    switch (ac.style) {
        case ArpStyle::Up: return 1;
        case ArpStyle::Down: return 2;
        case ArpStyle::UpDown: return 3;
        case ArpStyle::DownUp: return 4;
        case ArpStyle::AsPlayed: return 5;
        case ArpStyle::Random: return 6;
        case ArpStyle::ConvergeDiverge: return 7;
        default: return 1;
    }
}

void apply_astyle_zone(ArpCfg& ac, int zone) {
    if (zone < 0 || zone > 7) { return; }
    if (zone == 0) {
        ac.enabled = false;
        return;
    }
    ac.enabled = true;
    switch (zone) {
        case 1: ac.style = ArpStyle::Up; break;
        case 2: ac.style = ArpStyle::Down; break;
        case 3: ac.style = ArpStyle::UpDown; break;
        case 4: ac.style = ArpStyle::DownUp; break;
        case 5: ac.style = ArpStyle::AsPlayed; break;
        case 6: ac.style = ArpStyle::Random; break;
        case 7: ac.style = ArpStyle::ConvergeDiverge; break;
    }
}

int strum_zone(const ChordCfg& cc) {
    for (int i = 7; i >= 0; --i) {
        if (static_cast<int>(cc.strum_delay_ms) >= kStrumZones[i]) { return i; }
    }
    return 0;
}

void apply_strum_zone(ChordCfg& cc, int zone) {
    if (zone < 0 || zone > 7) { return; }
    cc.strum_delay_ms = static_cast<uint8_t>(kStrumZones[zone]);
}

int swing_zone(const TimingCfg& t) {
    for (int i = 7; i >= 0; --i) {
        if (static_cast<int>(t.swing_pct) >= kSwingZones[i]) { return i; }
    }
    return 0;
}

void apply_swing_zone(TimingCfg& t, int zone) {
    if (zone < 0 || zone > 7) { return; }
    t.swing_pct = static_cast<uint8_t>(kSwingZones[zone]);
}

int hum_zone(const TimingCfg& t) {
    for (int i = 7; i >= 0; --i) {
        if (static_cast<int>(t.humanize_ms) >= kHumZones[i]) { return i; }
    }
    return 0;
}

void apply_hum_zone(TimingCfg& t, int zone) {
    if (zone < 0 || zone > 7) { return; }
    t.humanize_ms = static_cast<uint8_t>(kHumZones[zone]);
}

int scale_full_index(const KeyFilterCfg& kf) {
    if (!kf.enabled) { return 0; }
    const uint8_t id = static_cast<uint8_t>(kf.scale);
    return id < kScaleFullCount ? id : 1;
}

void apply_scale_full(KeyFilterCfg& kf, int idx) {
    if (idx < 0 || idx >= kScaleFullCount) { return; }
    kf.scale = kScaleFullIds[idx];
    kf.enabled = (idx != 0);
}

int ctype_full_index(const ChordCfg& cc) {
    if (!cc.enabled) { return 0; }
    const uint8_t id = static_cast<uint8_t>(cc.type);
    return id < kCTypeFullCount ? id : 1;
}

void apply_ctype_full(ChordCfg& cc, int idx) {
    if (idx < 0 || idx >= kCTypeFullCount) { return; }
    cc.type = kCTypeFullIds[idx];
    cc.enabled = (idx != 0);
}

int astyle_full_index(const ArpCfg& ac) {
    if (!ac.enabled) { return 0; }
    const uint8_t id = static_cast<uint8_t>(ac.style);
    return id < kAStyleFullCount ? id : 1;
}

void apply_astyle_full(ArpCfg& ac, int idx) {
    if (idx < 0 || idx >= kAStyleFullCount) { return; }
    ac.style = kAStyleFullIds[idx];
    ac.enabled = (idx != 0);
}

}  // namespace

// ---------------------------------------------------------------------------
// QUICK rows
// ---------------------------------------------------------------------------

void build_quick_rows(AppState* st, MenuContent& c) {
    c.row_count = 0;
    c.item_count = 0;
    c.full_root = nullptr;
    c.full_root_count = 0;

    Pattern& p = st->active_pattern();

    auto add_row = [&](const char* label) -> QuickRow& {
        if (c.row_count >= kMaxQuickRows) {
            return c.rows[c.row_count - 1];
        }
        QuickRow& r = c.rows[c.row_count++];
        r = QuickRow {};
        r.label = label;
        r.seg_count = 0;
        return r;
    };
    auto add_seg = [&](QuickRow& r, const Segment& s) {
        r.segments[r.seg_count++] = s;
    };

    // ---- Mode row: MIDI keyboard <-> random note (click confirms the flip) ----
    {
        QuickRow& r = add_row("Mode");
        add_seg(r, Segment {SegmentType::Toggle, "Mode",
            [st]() { return st->runtime.mode == PlayMode::RandomNote ? 1 : 0; },
            [st](int32_t v) {
                st->runtime.mode = (v != 0) ? PlayMode::RandomNote : PlayMode::MidiKeyboard;
            },
            kModeLabels, 2, nullptr, 0, false});
    }

    // ---- Key row ----
    {
        QuickRow& r = add_row("Key");
        add_seg(r, Segment {SegmentType::Linear, "Key",
            [&p]() { return static_cast<int32_t>(p.key_filter.root_note); },
            [&p](int32_t v) { p.key_filter.root_note = static_cast<uint8_t>(v % 12); },
            kNoteNames, 12, nullptr, 0, false});

        add_seg(r, Segment {SegmentType::Radial, "Scale",
            [&p]() { return static_cast<int32_t>(scale_zone(p.key_filter)); },
            [&p](int32_t z) { apply_scale_zone(p.key_filter, static_cast<int>(z)); },
            kScaleRadialLabels, 8, nullptr, 0, false});

        const ItemRef ref = emit_key_block(c, p);
        add_seg(r, Segment {SegmentType::Param, "PRM", {}, {}, nullptr, 0, ref.items, ref.count, false});
    }

    // ---- Chord row (midi_keyboard / random_note / midi_filter) ----
    const PlayMode mode = st->runtime.mode;
    if (mode == PlayMode::MidiKeyboard || mode == PlayMode::RandomNote || mode == PlayMode::MidiFilter) {
        QuickRow& r = add_row("CHD");
        add_seg(r, Segment {SegmentType::Radial, "Type",
            [&p]() { return static_cast<int32_t>(ctype_zone(p.chord)); },
            [&p](int32_t z) { apply_ctype_zone(p.chord, static_cast<int>(z)); },
            kCTypeRadialLabels, 8, nullptr, 0, false});

        add_seg(r, Segment {SegmentType::Radial, "Strum",
            [&p]() { return static_cast<int32_t>(strum_zone(p.chord)); },
            [&p](int32_t z) { apply_strum_zone(p.chord, static_cast<int>(z)); },
            kStrumLabels, 8, nullptr, 0, false});

        const ItemRef ref = emit_chord_block(c, p);
        add_seg(r, Segment {SegmentType::Param, "PRM", {}, {}, nullptr, 0, ref.items, ref.count, false});
    }

    // ---- Arp row (midi_keyboard / midi_filter) ----
    if (mode == PlayMode::MidiKeyboard || mode == PlayMode::MidiFilter) {
        QuickRow& r = add_row("Arp");
        add_seg(r, Segment {SegmentType::Radial, "Style",
            [&p]() { return static_cast<int32_t>(astyle_zone(p.arp)); },
            [&p](int32_t z) { apply_astyle_zone(p.arp, static_cast<int>(z)); },
            kAStyleRadialLabels, 8, nullptr, 0, false});

        add_seg(r, Segment {SegmentType::Toggle, "Latch",
            [&p]() { return p.arp.latch ? 1 : 0; },
            [&p](int32_t v) { p.arp.latch = (v != 0); },
            kOnOffLabels, 2, nullptr, 0, false});

        const ItemRef ref = emit_arp_block(c, p);
        add_seg(r, Segment {SegmentType::Param, "PRM", {}, {}, nullptr, 0, ref.items, ref.count, false});
    }

    // ---- Timing row (affects the arp; click -> DETAIL) ----
    {
        QuickRow& r = add_row("Time");
        add_seg(r, Segment {SegmentType::Radial, "Swing",
            [&p]() { return static_cast<int32_t>(swing_zone(p.timing)); },
            [&p](int32_t z) { apply_swing_zone(p.timing, static_cast<int>(z)); },
            kSwingRadialLabels, 8, nullptr, 0, false});

        add_seg(r, Segment {SegmentType::Radial, "Hum",
            [&p]() { return static_cast<int32_t>(hum_zone(p.timing)); },
            [&p](int32_t z) { apply_hum_zone(p.timing, static_cast<int>(z)); },
            kHumRadialLabels, 8, nullptr, 0, false});

        const ItemRef ref = emit_timing_block(c, p);
        add_seg(r, Segment {SegmentType::Param, "PRM", {}, {}, nullptr, 0, ref.items, ref.count, false});
    }

    // ---- ADSR row (informative label, click -> DETAIL) ----
    {
        QuickRow& r = add_row("ADR");
        const ItemRef ref = emit_adsr_block(c, p);
        r.submenu = ref.items;
        r.submenu_count = ref.count;
        r.summary_fn = [&p]() { return p.gate.enabled ? "On" : "Off"; };
    }

    // ---- Dens / Shape rows (random modes) ----
    if (mode == PlayMode::RandomPattern || mode == PlayMode::RandomNote) {
        QuickRow& r = add_row("Dens");
        add_seg(r, Segment {SegmentType::Linear, "Dens",
            [&p]() { return static_cast<int32_t>(p.random.density_or_probability); },
            [&p](int32_t v) { p.random.density_or_probability = static_cast<uint8_t>(v); },
            nullptr, 101, nullptr, 0, false});

        QuickRow& r2 = add_row("Shape");
        add_seg(r2, Segment {SegmentType::Linear, "Shape",
            [&p]() { return static_cast<int32_t>(p.random.shape); },
            [&p](int32_t v) { p.random.shape = static_cast<uint8_t>(v); },
            kShapeLabels, 4, nullptr, 0, false});
    }
}

// ---------------------------------------------------------------------------
// MAIN (full menu) tree — organised by blocks, each block reuses the same
// item set as the QUICK DETAIL submenu.
// ---------------------------------------------------------------------------

void build_full_menu(AppState* st, MenuContent& c) {
    c.item_count = 0;
    c.row_count = 0;
    c.full_root = nullptr;
    c.full_root_count = 0;

    Pattern& p = st->active_pattern();

    // Collect child items for each block first. Their start indexes are
    // captured here and reused when the section headers are emitted below, so
    // the FULL root shows only the section headers and each one opens its own
    // grouped submenu (no flat, interleaved parameter list).
    std::size_t pat_start;
    int pat_count;

    std::size_t key_start;
    int key_count;

    std::size_t chord_start;
    int chord_count;

    std::size_t arp_start;
    int arp_count;

    std::size_t timing_start;
    int timing_count;

    std::size_t adsr_start;
    int adsr_count;

    std::size_t transp_start;
    int transp_count;

    std::size_t rnd_start;
    int rnd_count;

    std::size_t midi_start;
    int midi_count;

    std::size_t sys_start;
    int sys_count;

    // Pattern
    pat_start = c.item_count;
    emit(c, int_slider_io("Slot",
        [st]() { return static_cast<int32_t>(st->current_slot); },
        [st](int32_t v) { st->current_slot = static_cast<uint8_t>(v); }, 0, 15));
    emit(c, option_idx_io("Length",
        [&p]() { return static_cast<int32_t>((p.length / 16) - 1); },
        [&p](int32_t v) { p.length = static_cast<uint8_t>(16 * (static_cast<int>(v) + 1)); },
        kLengthLabels, 4));
    pat_count = static_cast<int>(c.item_count - pat_start);

    // Key / Scale
    key_start = c.item_count;
    emit_key_block(c, p);
    key_count = static_cast<int>(c.item_count - key_start);

    // Chord
    chord_start = c.item_count;
    emit_chord_block(c, p);
    chord_count = static_cast<int>(c.item_count - chord_start);

    // Arpeggiator
    arp_start = c.item_count;
    emit_arp_block(c, p);
    arp_count = static_cast<int>(c.item_count - arp_start);

    // Timing
    timing_start = c.item_count;
    emit_timing_block(c, p);
    timing_count = static_cast<int>(c.item_count - timing_start);

    // Gate / ADSR (same 6-parameter block as the Quick DETAIL submenu)
    adsr_start = c.item_count;
    emit_adsr_block(c, p);
    adsr_count = static_cast<int>(c.item_count - adsr_start);

    // Transpose (now also carries the MIDI-keyboard octave)
    transp_start = c.item_count;
    emit(c, int_slider_io("Semitones",
        [&p]() { return static_cast<int32_t>(p.transpose.semitones); },
        [&p](int32_t v) { p.transpose.semitones = static_cast<int8_t>(v); }, -12, 12));
    emit(c, int_slider_io("Octaves",
        [&p]() { return static_cast<int32_t>(p.transpose.octaves); },
        [&p](int32_t v) { p.transpose.octaves = static_cast<int8_t>(v); }, -4, 4));
    emit(c, int_slider_io("Base Octave",
        [st]() { return static_cast<int32_t>(st->runtime.base_octave); },
        [st](int32_t v) { st->runtime.base_octave = static_cast<uint8_t>(v); }, 1, 8));
    transp_count = static_cast<int>(c.item_count - transp_start);

    // Randomize (density + shape, mirrors the QUICK random rows)
    rnd_start = c.item_count;
    emit(c, int_slider_io("Density",
        [&p]() { return static_cast<int32_t>(p.random.density_or_probability); },
        [&p](int32_t v) { p.random.density_or_probability = static_cast<uint8_t>(v); }, 0, 100));
    emit(c, option_idx_io("Shape",
        [&p]() { return static_cast<int32_t>(p.random.shape); },
        [&p](int32_t v) { p.random.shape = static_cast<uint8_t>(v % 4); },
        kShapeLabels, 4));
    rnd_count = static_cast<int>(c.item_count - rnd_start);

    // MIDI (read-only placeholders)
    midi_start = c.item_count;
    emit(c, toggle_item_io("USB MIDI",
        []() { return 1; },
        [](int32_t) {}));
    emit(c, int_slider_io("Channel",
        []() { return 1; },
        [](int32_t) {}, 1, 16));
    midi_count = static_cast<int>(c.item_count - midi_start);

    // System: click-timing settings (persisted to flash) + the raw-input Test
    // screen. The sliders edit runtime.click directly; AppLoop persists them.
    sys_start = c.item_count;
    emit(c, int_slider_io("Debounce",
        [st]() { return static_cast<int32_t>(st->runtime.click.debounce_ms); },
        [st](int32_t v) { st->runtime.click.debounce_ms = static_cast<uint16_t>(v); }, 3, 60));
    emit(c, int_slider_io("Click 2x",
        [st]() { return static_cast<int32_t>(st->runtime.click.double_ms); },
        [st](int32_t v) { st->runtime.click.double_ms = static_cast<uint16_t>(v); }, 100, 1000));
    emit(c, int_slider_io("Click Lng",
        [st]() { return static_cast<int32_t>(st->runtime.click.long_ms); },
        [st](int32_t v) { st->runtime.click.long_ms = static_cast<uint16_t>(v); }, 200, 2000));
    emit(c, action_item("Test", [st]() { st->runtime.test_mode = true; }));
    sys_count = static_cast<int>(c.item_count - sys_start);

    // Second pass: emit the section headers contiguously. Root = these only.
    const std::size_t root_start = c.item_count;
    emit_section(c, "Pattern", pat_start, pat_count);
    emit_section(c, "Key / Scale", key_start, key_count);
    emit_section(c, "Chord", chord_start, chord_count);
    emit_section(c, "Arpeggiator", arp_start, arp_count);
    emit_section(c, "Timing", timing_start, timing_count);
    emit_section(c, "Gate / ADSR", adsr_start, adsr_count);
    emit_section(c, "Transpose", transp_start, transp_count);
    emit_section(c, "Randomize", rnd_start, rnd_count);
    emit_section(c, "MIDI", midi_start, midi_count);
    emit_section(c, "System", sys_start, sys_count);

    c.full_root = &c.items[root_start];
    c.full_root_count = static_cast<int>(c.item_count - root_start);
}

int direction_to_zone(Direction dir) {
    switch (dir) {
        case Direction::Up: return 0;
        case Direction::UpRight: return 1;
        case Direction::Right: return 2;
        case Direction::DownRight: return 3;
        case Direction::Down: return 4;
        case Direction::DownLeft: return 5;
        case Direction::Left: return 6;
        case Direction::UpLeft: return 7;
        default: return -1;
    }
}

}  // namespace drom
