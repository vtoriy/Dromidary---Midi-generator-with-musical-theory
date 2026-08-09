# Структуры данных (C++)

Все структуры объявлены в `src/types.hpp` (namespace `drom`). Модель — **статическая
и типизированная**: фиксированные массивы, без динамической аллокации в горячих
путях. В ОЗУ (RP2040, 264KB SRAM) живёт 16 слотов паттернов; клик-настройки
сохраняются во flash.

## Константы размера

```cpp
constexpr std::size_t kStepCountMax = 64;   // макс. длина паттерна
constexpr std::size_t kSlotCount    = 16;   // слотов паттернов
constexpr std::size_t kMaxChordNotes= 8;    // нот в аккорде
constexpr std::size_t kMaxArpNotes  = 64;   // длина арп-цикла
constexpr std::size_t kMaxHeldKeys  = 16;   // клавиш в live
constexpr std::size_t kKeyCount     = 16;   // нотных кнопок
```

## Step (шаг паттерна)

```cpp
struct Step {
    std::array<uint8_t, 4> notes {};   // MIDI note numbers; notes[0] = root
    uint8_t note_count {0};            // сколько нот заполнено
    bool active {false};               // есть ли звучащая нота на шаге
    bool tie {false};                  // продлить предыдущую ноту (не новый Note On)
    uint8_t length_steps {4};          // номинальная длина в шагах/16
};
```

## Pattern (паттерн)

```cpp
struct Pattern {
    std::array<Step, kStepCountMax> steps {};
    uint8_t length {16};               // 16/32/48/64
    KeyFilterCfg key_filter {};
    ChordCfg     chord {};
    ArpCfg       arp {};
    TimingCfg    timing {};
    GateCfg      gate {};
    TransposeCfg transpose {};
    RandomCfg    random {};
};
```

## Конфиги (все — вложенные структуры Pattern)

```cpp
struct KeyFilterCfg {
    bool enabled {false};              // по умолчанию выключен
    uint8_t root_note {0};             // 0..11 (C..B)
    ScaleId scale {ScaleId::Off};      // 16 ладов (см. enum ниже)
    SnapMode mode {SnapMode::SnapUp};  // SnapUp/SnapDown/Mute
};

struct ChordCfg {
    bool enabled {false};
    ChordType type {ChordType::Major}; // 26 типов + Off
    VoicingMode voicing {VoicingMode::Block}; // Block/Strum/Roll
    uint8_t strum_delay_ms {10};
};

struct ArpCfg {
    bool enabled {false};
    bool latch {false};
    RateMode rate_mode {RateMode::Note};  // Note (доли такта) / Ms (Free)
    uint8_t rate_note_index {6};          // индекс в kArpNoteDivs => "1/8"
    uint16_t rate_ms {100};               // Rate (Free): 10..2000, шаг 10
    uint8_t distance_semitones {12};      // Distance: интервал транспонирования, полутоны (0..48; 12 = октава)
    uint8_t steps {1};                    // Steps: кол-во доп. транспозиций (позиций = steps+1; 0 = только исходная высота)
    uint8_t cycle {8};                    // Cycle: длина цикла арпеджио в шагах (1..32)
    ArpStyle style {ArpStyle::Up};
};

struct TimingCfg {
    uint16_t bpm {120};          // темп, 20..300
    ClockSync clock {ClockSync::Off}; // MIDI Clock: Off / Master / Slave
    uint8_t swing_pct {0};       // задержка "off-beat" шага арпа (0..100)
    uint8_t humanize_ms {0};     // псевдослучайный сдвиг шага арпа (0..50)
    uint8_t quantize_grid {0};   // индекс в kQuantizeGrids (0 = Off)
    bool legato {false};         // перехлёст нот арпа (без разрыва)
};

struct GateCfg {
    bool enabled {false};
    uint16_t attack_ms {0};        // задержка Note On (0..2000)
    uint16_t decay_ms {0};         // зарезервировано (нет velocity-выхода)
    uint8_t sustain_pct {100};     // зарезервировано (нет velocity-выхода)
    uint16_t release_ms {0};       // продление Note Off (0..2000)
    bool sync_quantize {false};    // true = "quantize" sync (gate_cfg.sync)
};
```

> `attack_ms`/`release_ms` активны при `gate.enabled == true` и применяются в
> live-выводе через очередь отложенных событий `ModeEngine::pending_`
> (см. `02-midi-chain.md`, этапы 5–8). `decay_ms`/`sustain_pct` на MIDI не влияют.

struct TransposeCfg {
    int8_t semitones {0};              // -12..+12
    int8_t octaves {0};                // -4..+4
};

struct RandomCfg {
    uint8_t density_or_probability {50}; // 0..100
    uint8_t shape {0};                   // индекс в kShapeOptions (Asc/Desc/Arch/Rnd)
    uint8_t note_min {24};               // Note Range: нижняя граница (MIDI note)
    uint8_t note_max {108};              // Note Range: верхняя граница
};
```

> `density_or_probability` в alpha используется как вероятность появления ноты в
> потоке RandomNote (в UI — ячейка Dens); полноценная генерация случайных паттернов
> с диапазоном/длиной/velocity отложена (см. `07-roadmap-open-questions.md`).
> Диапазон нот (`note_min/note_max`) редактируется через `NoteRange`-пункт
> (`Randomize → Note Range`), границы 12..119 (C0..B8) — `kNoteRangeMin/Max`.

## Slot (слот хранения)

```cpp
struct AppState {
    std::array<Pattern, kSlotCount> slots {};
    uint8_t current_slot {0};
    RuntimeState runtime {};
    Pattern& active_pattern();        // slots[current_slot]
};
```

Один активный паттерн одновременно (без многодорожечности). Переключение слота =
замена `current_slot`; паттерн-воспроизведение в alpha не реализовано.

## RuntimeState (состояние выполнения)

```cpp
struct RuntimeState {
    PlayMode mode {PlayMode::MidiKeyboard};  // MidiKeyboard / RandomNote (в UI);
                                             // Pattern/RandomPattern/MidiFilter — зарезервированы
    ScreenMode screen_mode {ScreenMode::Quick}; // Quick/Full/Animation
    bool playing {false};
    bool recording {false};
    uint8_t current_step {0};
    uint8_t base_octave {2};        // распознаваемая октава нотных клавиш
    bool live_mute {false};         // Rest удерживается во время playback
    uint8_t last_note {0};          // последняя нота для статус-бара
    bool show_note {false};         // показывать ли ноту в статус-баре
    uint8_t last_input_note {0};    // RandomNote: последняя нажатая клавиша
    uint8_t func_bits {0};          // подтверждённый образ chip3 (bit i = raw bit 16+i)
    uint16_t note_bits {0};         // образ нотных клавиш (bit i = raw bit i)
    ClickSettings click {};         // тайминги кликов (сохраняются во flash)
    bool test_mode {false};         // активен экран Test (System → Test)
};
```

## ClickSettings (клик-настройки, сохраняются во flash)

```cpp
struct ClickSettings {
    uint16_t debounce_ms {12};  // фильтр дребезга функц. кнопок (3..60)
    uint16_t double_ms {300};   // окно двойного клика (100..1000)
    uint16_t long_ms {800};     // порог long-press джойстика (200..2000)
};
```

Сохранение: `persist.cpp`, последний flash-сектор
(`PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE`). Формат:

```
magic   = 0x44524F4D ("DROM")
version = 1
click   = ClickSettings
crc     = CRC32(click)
```

Загрузка при старте (`persist_load_click`): если magic/version не совпадают —
остаются дефолты из структуры. Запись (`persist_save_click`) — через
`flash_safe_execute`; AppLoop сохраняет через 500 мс после последнего изменения.

## Перечисления (enum class, значения — зеркала Python-списков)

```cpp
enum class PlayMode    { MidiKeyboard, Pattern, RandomPattern, RandomNote, MidiFilter };
enum class ScreenMode  { Quick, Full, Animation };
enum class ScaleId     { Off, Major, Minor, Dorian, Phrygian, Lydian, Mixolydian,
                         Locrian, HarmonicMinor, MelodicMinor, PentatonicMajor,
                         PentatonicMinor, Blues, WholeTone, Diminished, Chromatic, kCount };
enum class SnapMode    { SnapUp, SnapDown, Mute, kCount };
enum class ChordType   { Off, Major, Minor, Diminished, Augmented, Maj7, Min7, Dom7,
                         Min7b5, Dim7, Chord9, Chord11, Chord13, Maj9,
                         S7sh5, S7sh9, S7b9, S7sh11, Sus2, Sus4, S7sus4, Sus2_7,
                         Quartal, Quintal, Cluster, Power, kCount };
enum class VoicingMode { Block, Strum, Roll, kCount };
enum class ArpStyle    { Off, Up, Down, UpDown, DownUp, UpDownRep, DownUpRep,
                         Converge, Diverge, ConvergeDiverge,
                         PinkyUp, PinkyUpDown, ThumbUp, ThumbUpDown,
                         AsPlayed, ChordTrigger, Random, RandomOnce, RandomOther, kCount };
enum class RateMode    { Note, Ms, kCount };
enum class ClockSync   { Off, Master, Slave, kCount };  // Timing → Clock
```

## MenuItem / Segment (модель меню)

Меню строится в рантайме (`menu_items.cpp`) против активного паттерна; значения
редактируются live через getter/setter (`std::function`).

```cpp
enum class MenuItemType { Section, Group, Toggle, Option, IntSlider, Rate, NoteRange, Action };
struct MenuItem {
    MenuItemType type;
    const char* label;
    const MenuItem* children; int child_count;   // для Section/Group
    IntGetter get_i; IntSetter set_i;            // live чтение/запись
    int32_t min_v, max_v, step;
    const char* const* option_labels; int option_count;
    std::function<const char*(int32_t)> label_fn;  // динамическая подпись
    std::function<int32_t()> unit_get;             // Rate: 0=Note, 1=Ms
    std::function<void()> action;                  // Action-пункты
    // NoteRange: независимые границы (min/max). min_v..max_v = пределы 12..119.
    IntGetter get_min; IntSetter set_min;
    IntGetter get_max; IntSetter set_max;
};
```

Quick-ячейки:

```cpp
enum class SegmentType { Linear, Radial, Toggle, Param };
struct Segment {
    SegmentType type;
    const char* label;
    IntGetter get; IntSetter set;
    const char* const* labels; int count;
    const MenuItem* children; int child_count;   // для Param → DETAIL
    bool direct;                                 // правка сразу без клика
    std::function<const char*(int32_t)> label_fn;  // динамическая подпись ячейки
};
struct QuickRow { const char* label; std::array<Segment,3> segments; int seg_count;
                  const MenuItem* submenu; int submenu_count;
                  std::function<const char*()> summary_fn; };
```

Quick-ячейка `seg` может принимать значение **−1** = фокус на имени строки
(`menu_engine.cpp`): наклон влево с ячейки 0, `current_segment()` вернёт nullptr,
клик откроет DETAIL (`row.submenu`). В строке QUICK хранится её подменю
(`submenu/submenu_count`, заполняется из `emit_*_block`).

## Таблицы вариантов (label → enum)

- `kArpNoteDivs` (12, включая триоли): `"1/64","1/48","1/32","1/24","1/16","1/12","1/8","1/6","1/4","1/3","1/2","1/1"`.
- `kScaleFullLabels` (16) — соответствуют `ScaleId` в порядке enum.
- `kCTypeFullLabels` (26) — соответствуют `ChordType` (без Off), порядок enum.
- `kAStyleFullLabels` (19) — соответствуют `ArpStyle` в порядке enum (см. полный список в `types.hpp`).
- `kQuantizeLabels` (8): Off, 1/32, 1/16T, 1/16, 1/8T, 1/8, 1/4T, 1/4.
- `kShapeLabels` (4): Asc, Desc, Arch, Rnd.
- `kClockSyncLabels` (3): Off, Master, Slave.
- Радиальные (8 зон): `kScaleRadialLabels` = Off,Maj,Min,Dor,Phr,Lyd,Mix,Blu;
  `kCTypeRadialLabels` = Off,Maj,Min,Maj7,Min7,7,Sus4,Pow;
  `kAStyleRadialLabels` = Off,Up,Down,UpDn,DnUp,Play,Rnd,CvDv;
  `kStrumLabels` = Off,10,20,30,40,50,75,100 (мс).