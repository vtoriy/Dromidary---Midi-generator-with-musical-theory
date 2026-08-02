# 🎹 dromidary — MIDI Groovebox / Sequencer for Raspberry Pi Pico

```
 ______  __  __   ____   ____   ____    __    __   ____    ______   __    __   ____
/\__  _\/\ \/\ \ /\  _`\/\  _`\/\  _`\ /\ \  /\ \ /\  _`\ /\  _  \ /\ \  /\ \ /\  _`\
\/_/\ \/\ \ \_\ \\ \ \L\ \ \ \L\ \ \,\L\_\ \ \ \ \ \ \ \L\_\ \ \L\ \\ \ \ \ \ \\ \ \L\_\
   \ \ \ \ \  _  \\ \ ,__/\ \ ,__/\ \_\ \_\ \ \ \ \ \ \ \ \L\ \ \  __ \ \ \ \ \ \\ \  _\L
    \ \ \ \ \ \ \ \\ \ \\ /\ \ \\ /\ \ \\ \ \ \_\ \ \ \ \_\ \ \ \ \ \ \ \ \ \ \\ \ \L\ \
     \ \_\ \ \_\ \_\ \_\ \ \ \_\ \ \ \ \ \ \ \_____\ \ \____/\ \_\ \_\ \ \____ \ \_\ \ \____/
      \/_/  \/_/\/_/\/_/  \/_/   \/  \_\_\/______ \/___/  \/_/\/_/ \/____/ \/_/\/___/\/___/
```

[![License: MIT](https://img.shields.io/badge/License-MIT-green.svg)](LICENSE)
[![Language: Python](https://img.shields.io/badge/Language-Python-3776AB.svg)]()
[![MCU: RP2040](https://img.shields.io/badge/MCU-RP2040-DC143C.svg)]()
[![Firmware: CircuitPython](https://img.shields.io/badge/CircuitPython-10.0.3-1E90FF.svg)]()
[![MIDI: USB](https://img.shields.io/badge/MIDI-USB%20MIDI-orange.svg)]()
[![Status: active](https://img.shields.io/badge/Status-active-brightgreen.svg)]()

Аппаратный **MIDI-контроллер / секвенсор / грувбокс** на базе **Raspberry Pi Pico
(RP2040)**. Прошивка написана на **Adafruit CircuitPython 10.0.3**.

Устройство **не содержит звукового движка** — это чистый **MIDI-генератор и
процессор**:

> 🎛️ Принимает ввод с **16 нотных кнопок**, **6 функциональных кнопок** и
> **джойстика KY-023** → обрабатывает через конвейер (тональность, аккорды,
> арпеджио, тайминг) → отправляет **MIDI-события** по **USB MIDI**.
> В перспективе — аппаратный **UART MIDI DIN**.

---

## ✨ Возможности

| 🚀 | Фича | Что делает |
|---|---|---|
| 🎼 | **Key / Scale Quantizer** | Фильтр нот по тональности: snap-up, snap-down или mute «неправильной» ноты |
| 🎹 | **Chord Builder** | 26 типов аккордов (трезвучия, 7-е, sus, альтерации, quartal/quintal, cluster, power) + манеры block / strum / roll |
| ⏺️ | **Pattern Sequencer** | Запись в реальном времени (overdub), пошаговое редактирование, 16 слотов в ОЗУ |
| 🔁 | **Arpeggiator** | 7 стилей (Up, Down, Up-Down, Down-Up, As-Played, Random, Converge/Diverge), частота в нотах (1/64–1/1) или в мс, диапазон, длина цикла, **latch** |
| 🎚️ | **ADSR Gate** | Attack/Decay/Sustain/Release как модель gate-тайминга (без velocity) |
| ⬆️ | **Transpose** | Сдвиг в полутонах и октавах (до Key Filter) |
| 🎲 | **Random Generator** | Случайные паттерны и ноты с настраиваемой плотностью, диапазоном, формой |
| ⏱️ | **Timing FX** | Swing/groove, humanize, триоли/квантизация, legato |
| 🥁 | **BPM & Time Signature** | Темп и размер такта, внутренний генератор |
| 🔄 | **MIDI IN → MIDI OUT** | Устройство как MIDI-процессор «в разрыв» |

---

## 🧬 Архитектура конвейера обработки ноты

Порядок этапов фиксирован и согласован с заказчиком:

```
Нотная клавиша
     │
     ▼
┌────────────┐   ┌────────────┐   ┌──────────────┐   ┌──────────────────┐   ┌────────────┐
│ Key Filter │──▶│   Chord    │──▶│  Arpeggiator │──▶│ Timing (swing /   │──▶│ Gate / ADSR │
│ (тональн.)  │   │  Builder   │   │  + range     │   │  humanize / quant │   │  (тайминг)  │
└────────────┘   └────────────┘   └──────────────┘   │  / legato)        │   └────────────┘
                                                      └──────────────────┘        │
                                                                                 ▼
                                                                          USB MIDI OUT
```

> ℹ️ Подробно: [`midi-groovebox-docs/02-midi-chain.md`](midi-groovebox-docs/02-midi-chain.md)

---

## 🕹️ Режимы работы

| Режим | Описание |
|---|---|
| 🎹 `midi_keyboard` | Клавиатура: ноты → аккорды/арпеджио live |
| ⏺️ `pattern` | Воспроизведение записанного паттерна |
| 🎲 `random_pattern` | Генерация и игра случайного паттерна |
| 🎵 `random_note` | Случайные ноты из диапазона/формы |
| 🔄 `midi_filter` | MIDI IN → конвейер → MIDI OUT |

См. матрицу доступности: [`06-mode-matrix.md`](midi-groovebox-docs/06-mode-matrix.md)

---

## 🧰 Аппаратная платформа

| Компонент | Детали |
|---|---|
| 🧠 MCU | Raspberry Pi Pico, **RP2040** |
| 💾 Прошивка | Adafruit CircuitPython **10.0.3** |
| 🖥️ Дисплей | OLED **SH1106**, I2C, 132×64 px |
| 🎹 Нотные кнопки | **16** шт. (12 нот + октавы) |
| ⚙️ Функциональные кнопки | **6** шт. (Play, Record, Shift, Rest, Oct Down, Oct Up) |
| 🔌 Чтение кнопок | 2 каскадных **74HC165** (24 бита) |
| 🕹️ Джойстик | **KY-023** (аналоговый X/Y + кнопка) |
| 🎧 MIDI | **USB MIDI** (сейчас); **UART MIDI DIN** (план) |

> ⚠️ **Известный аппаратный баг**: на втором 74HC165 перепутаны провода в диапазоне
> D4–D7. Подробности — [`01-hardware.md`](midi-groovebox-docs/01-hardware.md).

---

## 🖱️ Управление

- **Нотные клавиши** — игра/запись нот; с Shift — числовой ввод значений.
- **Кнопки**: Play/Stop, Record (overdub), Shift, Rest (тишина/пауза), Octave.
- **Джойстик** — навигация по меню, радиальный селектор аккордов/арпеджио,
  ввод значений (удержание = авто-повтор с ускорением).
- **Экраны**: Quick (быстрые настройки) → Full/MAIN (полное меню) → Animation.

---

## 📦 Установка на Pico

1. ⬇️ Установите **Adafruit CircuitPython 10.x** на Raspberry Pi Pico
   ([circuitpython.org](https://circuitpython.org/board/raspberry_pi_pico/)).
2. 📁 Скопируйте `code.py` и все `*.py` в корень диска `CIRCUITPY`.
3. 📂 Скопируйте содержимое `lib/` в папку `lib/` на диске.
4. 🔌 Подключите дисплей / кнопки / джойстик по распиновке из
   [`midi-groovebox-docs/01-hardware.md`](midi-groovebox-docs/01-hardware.md).
5. 🚀 Подключите Pico по USB — устройство появится как MIDI-инструмент.

---

## 🗂️ Структура проекта

| Файл | Назначение |
|---|---|
| `code.py` | Главный цикл: чтение ввода, дисплей, роутинг событий |
| `hardware.py` | Чтение 74HC165 и джойстика KY-023 |
| `midi.py`, `midi_chain.py` | MIDI-вывод и конвейер обработки ноты |
| `key_filter.py` | Фильтр по тональности |
| `chord_builder.py` | Генератор аккордов + voicing |
| `arpeggiator.py` | Стили арпеджио и диапазон |
| `timing_effects.py` | Swing, humanize, quantize, legato |
| `gate_adsr.py` | ADSR как gate-тайминг |
| `mode_engine.py` | Конечный автомат режимов + live-арпеджио |
| `sequencer.py` | Паттерн-секвенсор (play/stop, scheduling) |
| `state.py`, `state_manager.py` | Структуры данных паттерна/слотов |
| `menu.py`, `renderer.py`, `display.py` | Меню (Quick/DETAIL/MAIN/Animation) и рендер |
| `lib/` | Библиотеки Adafruit (`.mpy`) для дисплея |

---

## 📚 Документация

Полная документация — в [`midi-groovebox-docs/`](midi-groovebox-docs/README.md).
Рекомендуемый порядок чтения:

1. 🌐 [Обзор проекта](midi-groovebox-docs/00-overview.md) — что это и принятые решения
2. 🔧 [Аппаратура](midi-groovebox-docs/01-hardware.md) — распиновка, кнопки, план UART
3. 🧬 [Конвейер обработки ноты](midi-groovebox-docs/02-midi-chain.md) — главная логика
4. 🧱 [Структуры данных](midi-groovebox-docs/03-data-structures.md) — pattern/step/slot
5. 🖱️ [Навигация меню](midi-groovebox-docs/04-menu-navigation.md) — экраны, клик-циклы
6. 🎛️ [Распределение ввода](midi-groovebox-docs/05-input-mapping.md) — кнопки, джойстик
7. 🔀 [Матрица режимов](midi-groovebox-docs/06-mode-matrix.md) — доступность функций
8. 🗺️ [Роадмап](midi-groovebox-docs/07-roadmap-open-questions.md) — что дальше

---

## 🗺️ Статус и планы

**Сделано:** конвейер ноты, секвенсор, меню, 5 режимов, live-арпеджио, автоповтор
джойстика, объединённый Rate, полифонический арпеджио + Key/Chord.

**В планах:** step-edit паттерна, отладка random-режимов на железе, **UART MIDI OUT**,
MIDI IN + оптрон, **MIDI Clock sync**, сохранение слотов на flash/ПЗУ.

---

## 📄 Лицензия

Проект распространяется под лицензией **MIT** — см. [LICENSE](LICENSE).
