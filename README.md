# 🎹 dromidary — MIDI Groovebox / Sequencer for Raspberry Pi Pico

[![License: MIT](https://img.shields.io/badge/License-MIT-green.svg)](LICENSE)
[![Language: C++](https://img.shields.io/badge/Language-C%2B%2B17-00599C.svg)]()
[![MCU: RP2040](https://img.shields.io/badge/MCU-RP2040-DC143C.svg)]()
[![Firmware: Pico SDK](https://img.shields.io/badge/Pico_SDK-1.5.x-1E90FF.svg)]()
[![MIDI: USB](https://img.shields.io/badge/MIDI-USB%20MIDI-orange.svg)]()
[![Build: alpha](https://img.shields.io/badge/Status-alpha-brightgreen.svg)]()

Аппаратный **MIDI-контроллер / грувбокс** на базе **Raspberry Pi Pico (RP2040)**.
Прошивка — на **C++17 (Pico SDK 1.5.x)** + **TinyUSB**.

> Устройство **не содержит звукового движка** — это чистый **MIDI-генератор и
> процессор**: принимает ввод с **16 нотных кнопок**, **6 функциональных кнопок**
> и **джойстика KY-023**, обрабатывает его через конвейер (тональность → аккорды →
> арпеджио) и отправляет **MIDI Note On/Off** и **CC** по **USB MIDI**.

---

## ✨ Возможности (alpha)

| 🚀 | Фича | Что делает |
|---|---|---|
| 🎼 | **Key / Scale Quantizer** | 16 ладов; «неправильные» ноты: snap-up, snap-down или mute |
| 🎹 | **Chord Builder** | 26 типов аккордов (triads, 7th, sus, altered, quartal/quintal, cluster, power) |
| 🔁 | **Arpeggiator** | 19 стилей (Up/Down, UpDown/DownUp, Up&Dn/Dn&Up, Converge/Diverge/Con&Div, Pinky/Thumb-педали, As Played, Chord Trigger, Random/Random Once/Random Other, Off), rate в нотах (1/64–1/1, включая триоли) или мс (Free); **Distance** = шаг транспонирования (полутоны), **Steps** = доп. транспозиции, **Cycle** = длина цикла, фильтр по тональности, **latch** |
| 🎵 | **RandomNote** | Непрерывная генерация случайных нот вокруг якоря клавиши/Play, с фильтром в тональность |
| 🎚️ | **Полифонический live-арпеджио** | Все зажатые клавиши объединяются в один аккорд → единый арп-цикл |
| ⬆️ | **Transpose / Octave** | Полутоны и октавы (до Key Filter); базовая октава клавиатуры 1–8 |
| 🎛️ | **Quick / Detail / Main меню** | Быстрые ячейки, радиальный селектор (8 зон), полное дерево, сброс Rest+клик |
| ⚙️ | **Persist во flash** | Тайминги кликов (debounce/double/long) сохраняются в последний сектор |
| 🎛️ | **Timing-эффекты** | Swing/humanize/quantize/legato — применяются к live-арпеджио |
| 🎚️ | **Gate/ADSR** | Attack задерживает Note On, release продлевает Note Off |
| 🎸 | **Voicing** | Block / Strum / Roll (с `strum_delay_ms`) для аккордов в live |
| 🔬 | **Экран Test** | Диагностика распиновки кнопок (System → Test), выход Shift+клик |
| 🎚️ | **CC-дубли** | Функц. кнопки дублируются MIDI CC 20–25 на канале 16 для DAW |

**Не реализовано (отложено):** паттерн-секвенсор (запись/воспроизведение),
режимы Random Pattern / Pattern / MIDI Filter, UART MIDI DIN, MIDI Clock,
сохранение паттернов. Timing/ADSR/voicing — реализованы (см. таблицу выше).
Подробно — [`midi-groovebox-docs/07-roadmap-open-questions.md`](midi-groovebox-docs/07-roadmap-open-questions.md).

---

## 🧬 Конвейер обработки ноты

Порядок фиксирован (детали — [`02-midi-chain.md`](midi-groovebox-docs/02-midi-chain.md)):

```
Нотная клавиша (или RandomNote)
      │
      ▼
Transpose ──► Key Filter ──► Chord Builder ──► secondary filter ──► Arpeggiator
(± полутоны/октавы)  (16 ладов)  (26 типов)   (структура в лад)     (live-цикл)
      │                                                              │
      ▼                                                              ▼
  Timing (swing/humanize/quantize/legato)                     Gate/ADSR + Voicing
                                                              (attack/release, Strum/Roll)
      └───────────────────────────────────────────────────────────────▼
                                                                  USB MIDI OUT
```

---

## 🕹️ Режимы (в alpha доступны 2)

| Режим | Как включить | Описание |
|---|---|---|
| 🎹 **MIDI-клавиатура (KB)** | по умолчанию | Live-игра: Key Filter → аккорды → полифонический арпеджио |
| 🎵 **Случайная нота (RND)** | Quick → ячейка **Mode** | Непрерывный поток случайных нот вокруг якоря нажатой клавиши |

`Pattern`, `RandomPattern`, `MidiFilter` — зарезервированы (требуют секвенсора и
MIDI IN). См. [`06-mode-matrix.md`](midi-groovebox-docs/06-mode-matrix.md).

---

## 🧰 Аппаратная платформа

| Компонент | Детали |
|---|---|
| 🧠 MCU | Raspberry Pi Pico, **RP2040** |
| 💾 Прошивка | C++17, **Pico SDK 1.5.x**, TinyUSB |
| 🖥️ Дисплей | OLED **SH1106**, I2C0, 132×64 px, адрес 0x3C |
| 🎹 Нотные кнопки | **16** шт. (1–12 = C..B текущей октавы, 13–16 = C–D# октавой выше) |
| ⚙️ Функц. кнопки | **6** шт.: Play, Rest, Record, Shift, Oct Down, Oct Up |
| 🔌 Чтение кнопок | Каскад **74HC165** (24 бита, Latch=GP2, Clock=GP3, Data=GP4) |
| 🕹️ Джойстик | **KY-023** (X=GP26, Y=GP27, кнопка=GP15) |
| 🎧 MIDI | **USB MIDI** (сейчас); **UART MIDI DIN** (план) |

> ⚠️ **Известный аппаратный баг**: на втором 74HC165 перепутаны провода в
> диапазоне D4–D7. Оставлено как есть (физическая разводка настроена).
> Подробности — [`01-hardware.md`](midi-groovebox-docs/01-hardware.md).

---

## 🖱️ Управление

- **Нотные клавиши** — игра нот; октава — Oct Up/Dn.
- **Play** — старт/стоп (в RND — запуск/остановка цикла случайной ноты);
  **Rest** — live-mute пока зажата; **Rest + клик джойстика** — сброс значения.
- **Джойстик** — навигация по меню, радиальный селектор (Scale/Chord/Style/Strum),
  ввод значений (±1), удержание — авто-повтор, Shift+наклон — крайние значения.
- **Экраны**: Quick (быстрые ячейки) → Full/MAIN (полное меню) → Animation —
  long-press джойстика на корне.
- **Экран Test** — Full → System → Test (диагностика распиновки, выход Shift+клик).

---

## 📦 Установка на Pico

Прошивка собирается из исходников C++ (Pico SDK). Подробные шаги — в
[`firmware-cpp-alpha/README.md`](firmware-cpp-alpha/README.md). Коротко:

1. ⬇️ Установите тулчейн: **CMake 3.13+**, **Ninja**, **ARM GCC** (arm-none-eabi).
2. 📦 Склонируйте **Pico SDK 1.5.x** и задайте `PICO_SDK_PATH`.
3. 🔨 Соберите:

   ```sh
   cmake -B build -G Ninja
   cmake --build build
   ```

4. 🔌 Зажмите **BOOTSEL** и подключите Pico по USB → появится диск `RPI-RP2`.
5. 📁 Скопируйте `build/dromidary_cpp_alpha.uf2` на диск `RPI-RP2`.

После перезагрузки устройство появится как **USB MIDI**-инструмент.

---

## 🗂️ Структура проекта

```
firmware-cpp-alpha/          ← прошивка (C++17, Pico SDK)
├── src/
│   ├── main.cpp, app_loop.* → точка входа и главный цикл
│   ├── types.hpp            → все структуры данных/конфиги
│   ├── persist.*            → клик-настройки во flash (CRC32)
│   ├── engine/              → key_filter, chord_builder, arpeggiator, midi_chain
│   ├── mode/                → mode_engine (live-игра, арпеджио, RandomNote)
│   ├── menu/                → menu_items, menu_engine, renderer, animation
│   ├── platform/            → shift165, joystick, display_sh1106, usb_midi, board_pins
│   └── state/               → AppState, init_default_state
├── CMakeLists.txt           → сборка
└── pico_sdk_import.cmake
midi-groovebox-docs/         ← документация (рус.)
└── 00-overview … 07-roadmap
```

---

## 📚 Документация

Полная документация (рус.) — в [`midi-groovebox-docs/`](midi-groovebox-docs/README.md):

1. [Обзор проекта](midi-groovebox-docs/00-overview.md) — что это, принятые решения, статус
2. [Аппаратура](midi-groovebox-docs/01-hardware.md) — распиновка, битовая карта, Test
3. [Конвейер ноты](midi-groovebox-docs/02-midi-chain.md) — главная логика
4. [Структуры данных](midi-groovebox-docs/03-data-structures.md) — C++-модель
5. [Навигация меню](midi-groovebox-docs/04-menu-navigation.md) — Quick/DETAIL/MAIN/Anim
6. [Распределение ввода](midi-groovebox-docs/05-input-mapping.md) — кнопки, джойстик
7. [Матрица режимов](midi-groovebox-docs/06-mode-matrix.md) — KB/RND
8. [Роадмап](midi-groovebox-docs/07-roadmap-open-questions.md) — что дальше

---

## 📄 Лицензия

Проект распространяется под лицензией **MIT** — см. [LICENSE](LICENSE).
