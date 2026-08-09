# Навигация меню и режимы экрана

## Разновидности экрана

1. **QUICK** (`Quick |`) — панель быстрых параметров (Level 1): строки-группы
   (Mode/Key/CHD/Arp/ADR/Dens/Shape) с ячейками. Ячейка `PRM` открывает DETAIL.
2. **DETAIL** (`<...` — бэк-ссылка) — подменю из `PRM` (Level 2): детальные
   параметры группы с полными списками вариантов.
3. **MAIN** (`Main |`) — полное меню: дерево всех параметров устройства.
4. **ANIM** (`Anim |`) — декоративный режим (скринсейвер), не редактируется.

DETAIL и MAIN используют общую модель «список параметров» (MenuItem). Радиальный
селектор — **только в QUICK**; в DETAIL/MAIN — только регулярный выбор
варианта влево/вправо.

Переключение между экранами происходит на верхнем уровне навигации: **long-press
джойстика на корне** текущего экрана (Quick → Full → Animation → Quick).

## Управление

| Действие | Эффект |
|---|---|
| Короткий клик джойстика | Войти в выбранный параметр / подтвердить выбор / вход в edit-mode ячейки |
| Короткий наклон джойстика влево/вправо | Изменить значение параметра на ±1 шаг (в DETAIL/MAIN — сразу) |
| Удержание джойстика (long-press), стек не пуст | `pop()` — подняться на уровень выше |
| Удержание джойстика (long-press), стек пуст (корень) | Переключить screen_mode по кругу |
| Rest + клик джойстика | Сброс значения к снимку (в любом меню) |
| Двойной клик (~double_ms, 300 мс) | Сброс в DETAIL/MAIN (доп. к Rest+клик) |
| Shift + наклон | Прыжок к крайнему значению (min/max) |
| Удержание джойстика в направлении | Авто-повтор с ускорением: ~220 мс → 150 → 110 → 60 мс, на 3-м уровне по 3 шага за тик (`app_loop.cpp`) |

Тайминги кликов настраиваются: System → Debounce / Click 2x / Click Lng
(см. `03-data-structures.md`, ClickSettings) и сохраняются во flash.

## Стек навигации

`MenuEngine` держит стек кадров (`stack_`, глубина до 5). Каждый кадр — либо
набор строк QUICK (`is_rows`), либо список `MenuItem` (DETAIL/MAIN). `rebuild()`
полностью перестраивает содержимое при смене режима/паттерна/test.

## Радиальный селектор (аккорды/арпеджио/лад/струм)

Когда пользователь входит в edit mode радиальной ячейки QUICK (Scale, CType,
AStyle, Strum), джойстик работает как радиальный селектор из **8 секторов по
направлению** (см. `menu_items.cpp` — `direction_to_zone()`):

| Zone | Направление | Scale | CType | AStyle | Strum |
|---|---|---|---|---|---|
| 0 | Вверх | **Off** | **Off** | **Off** | **Off** |
| 1 | Вверх-вправо | Major | Maj | Up | 5 мс |
| 2 | Вправо | Minor | Min | Down | 10 |
| 3 | Вниз-вправо | Dorian | Maj7 | Up-Down | 15 |
| 4 | Вниз | Phrygian | Min7 | Down-Up | 20 |
| 5 | Вниз-влево | Lydian | Dom7 | As Played | 25 |
| 6 | Влево | Mixolydian | Sus4 | Random | 30 |
| 7 | Вверх-влево | Blues | Power | Converge/Diverge | 35 |

Зона 0 (вверх) в Scale/CType/AStyle = **Off** (блок выключен). Выбор Scale≠Off
автоматически включает Key Filter; CType≠Off включает Chord; AStyle≠Off включает
Arp (отдельных свитчей в QUICK нет — в DETAIL/MAIN есть).

## QUICK — главный экран (реально построенные строки, `build_quick_rows`)

Строки строятся по активному режиму:

| Строка | Ячейки | Примечание |
|---|---|---|
| **Mode** | Toggle `KB` / `RND` | переключение MIDI-клавиатура ↔ случайная нота (клик подтверждает) |
| **Key** | Linear `Key` (12 нот) + Radial `Scale` + `PRM` | Scale — зоны, Key — линейный (только выбираемая нота) |
| **CHD** (только KB/RND/MidiFilter) | Radial `CType` + Radial `Strum` + `PRM` | порох, блок аккордов |
| **Arp** (только KB/MidiFilter) | Radial `AStyle` + Toggle `Latch` + `PRM` | арпеджиатор |
| **Time** (всегда) | Radial `Swing` + Radial `Quant` + `PRM` | тайминг, влияет на арп; PRM → BPM/Swing/Human/Quant/legato |
| **ADR** (всегда) | сводка On/Off, клик → DETAIL | блок ADSR: Switch, Atk, Dec, Sus, Rel, Sync |
| **Dens** (только RND) | Linear `Dens` (0..100) | плотность/вероятность |
| **Shape** (только RND) | Linear `Shape` (Asc/Desc/Arch/Rnd) | форма |

Клик-цикл ячеек QUICK (кроме чеков/`PRM`): клик → edit mode (ячейка выделяется),
джойстик меняет значение (линейные — влево/вправо, радиальные — зоны), повторный
клик применяет и выходит. В edit-mode при отличии от снимка добавляется маркер.

**DETAIL/MAIN**: значения редактируются **сразу** (без клика); переход на другой
пункт «принимает» значение. Если в edit-mode значение отличается от снапшота —
показывается `!` / `+-`.

## Полное дерево Full Menu (MAIN)

Построение — `build_full_menu()` в `menu_items.cpp`. Root содержит только
заголовки разделов, каждый открывает своё сгруппированное подменю:

```
Main
├── Pattern
│   ├── Slot            (0–15)
│   └── Length          (16 / 32 / 48 / 64)
├── Key / Scale
│   ├── Key Filter      (On / Off)
│   ├── Root Note       (C..B, 12 нот)
│   ├── Scale           (полные 16: Off, Maj, Min, Dor, Phr, Lyd, Mix, Loc,
│   │                    HMin, MMin, PMaj, PMin, Blu, WhT, Dim, Chr)
│   └── Snap            (Up / Dn / Mute)
├── Chord
│   ├── Chord           (On / Off)
│   ├── Type            (полные 26: Off, Maj, Min, Dim, Aug, Maj7, Min7, 7(other),
│   │                    m7b5, Dim7, 9, 11, 13, Maj9, 7#5, 7#9, 7b9, 7#11,
│   │                    Sus2, Sus4, 7s4, s2/7, Qrt, Qnt, Cls, Pow)
│   ├── Voicing         (Blk / Strm / Roll)
│   └── Strum           (1–100 мс)
├── Arpeggiator
│   ├── Arp             (On / Off)
│   ├── Latch           (On / Off)
│   ├── Style           (полные 19: Off, Up, Down, UpDn, DnUp, Up&Dn, Dn&Up,
│   │                    Converge, Diverge, C&Div, PinkUp, PinkDn, ThmbUp,
│   │                    ThmbUD, PlayOrd, Chord, Rnd, Rnd1, RndO)
│   ├── RateMode        (Note / Free)
│   ├── Rate            (Note: 1/64..1/1 все, включая триоли; Free: 10–2000 мс шаг 10)
│   ├── Distance        (0–48 полутонов; шаг транспонирования, 12 = октава)
│   ├── Steps           (0–16 доп. транспозиций; позиций = Steps+1)
│   └── Cycle           (1–32 шагов)
├── Timing
│   ├── BPM             (20–300)
│   ├── Swing           (0–100%)
│   ├── Humanize        (0–50 мс)
│   ├── Quantize        (Off, 1/32, 1/16T, 1/16, 1/8T, 1/8, 1/4T, 1/4)
│   └── Legato          (On / Off)
├── Gate / ADSR
│   ├── Switch          (On / Off)
│   ├── Atk             (0–500 мс)
│   ├── Dec             (0–500 мс)
│   ├── Sus             (0–100%)
│   ├── Rel             (0–500 мс)
│   └── Sync            (Note / Ms)
├── Transpose
│   ├── Semitones       (−12 .. +12)
│   ├── Octaves         (−4 .. +4)
│   └── Base Octave     (1–8)
├── Randomize
│   ├── Density         (0–100%)
│   └── Shape           (Asc / Desc / Arch / Rnd)
├── MIDI
│   ├── USB MIDI        (On, readonly)
│   └── Channel         (1, readonly — placeholder)
└── System
    ├── Debounce        (3–60 мс)
    ├── Click 2x        (100–1000 мс)
    ├── Click Lng       (200–2000 мс)
    └── Test            (открывает экран диагностики ввода)
```

> Пункты Timing/ADSR/MIDI-подразделов в alpha — только настройки в структурах;
> к MIDI-выводу применяется лишь часть конвейера (см. `02-midi-chain.md`).

## Animation Mode

Не содержит редактируемых параметров. Управление: long-press джойстика на корне —
смена экрана по кругу. Кнопки Play/Rec не блокируются — транспорт в alpha
ограничен (паттерн-секвенсор не реализован, см. `07-roadmap-open-questions.md`).