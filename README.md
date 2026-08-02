# dromidary — MIDI Groovebox on Raspberry Pi Pico (RP2040)

Аппаратный MIDI-контроллер/секвенсор/грувбокс на базе Raspberry Pi Pico (RP2040),
прошивка на **Adafruit CircuitPython 10.0.3**. Устройство не содержит звукового
движка — это чистый MIDI-генератор/процессор: принимает ввод с физических кнопок и
джойстика и отправляет MIDI-события через **USB MIDI** (в перспективе — аппаратный
UART MIDI DIN).

## Возможности

- **Фильтр по тональности** (key/scale quantizer): snap_up / snap_down / mute.
- **Генератор аккордов** (трезвучия, септаккорды, sus, альтерации, quartal/quintal,
  cluster, power) с манерами игры block / strum / roll.
- **Паттерн-секвенсор**: запись в реальном времени (overdub), пошаговое
  редактирование, 16 слотов в ОЗУ.
- **Арпеджиатор**: стили Up/Down/Up-Down/Down-Up/As-Played/Random/Converge-Diverge,
  частота по ноте (1/64–1/1) или в мс, диапазон, длина цикла, latch.
- **ADSR** как модель gate-тайминга (без velocity).
- **Транспонирование** в полутонах/октавах.
- **Генератор случайных паттернов/нот** с настраиваемой рандомизацией.
- **Timing**: swing/groove, humanize, триоли/квантизация, legato.
- **BPM** и размерность такта.
- **MIDI IN → MIDI OUT** (устройство как MIDI-процессор в разрыв цепи).

## Аппаратная платформа

| Компонент | Детали |
|---|---|
| MCU | Raspberry Pi Pico, RP2040 |
| Прошивка | Adafruit CircuitPython 10.0.3 |
| Дисплей | OLED SH1106, I2C, 132×64 px |
| Нотные кнопки | 16 шт. + 6 функциональных (Play, Record, Shift, Rest, Oct Down, Oct Up) |
| Чтение кнопок | 2 каскадных сдвиговых регистра 74HC165 (24 бита) |
| Джойстик | KY-023 (аналоговый X/Y + кнопка) |
| MIDI | USB MIDI (сейчас); UART MIDI DIN (план) |

## Структура проекта

- `code.py` — главный цикл: чтение кнопок/джойстика, дисплей, роутинг событий.
- `hardware.py` — чтение 74HC165 и джойстика KY-023.
- `midi.py`, `midi_chain.py` — MIDI-вывод и конвейер обработки ноты.
- `key_filter.py`, `chord_builder.py`, `arpeggiator.py`, `timing_effects.py`,
  `gate_adsr.py` — этапы конвейера (Key Filter → Chord → Arp → Timing → Gate/ADSR).
- `mode_engine.py` — конечный автомат режимов и live-арпеджио.
- `sequencer.py` — паттерн-секвенсор (play/stop, scheduling).
- `state.py`, `state_manager.py` — структуры данных паттерна/слотов и управление.
- `menu.py`, `renderer.py`, `display.py` — меню (Quick/DETAIL/MAIN/Animation) и рендер.
- `lib/` — библиотеки Adafruit (`.mpy`) для дисплея.
- `midi-groovebox-docs/` — полная документация (см. ниже).

## Установка на Pico

1. Установите Adafruit CircuitPython 10.x на Raspberry Pi Pico.
2. Скопируйте `code.py` и все `*.py` в корень диска CIRCUITPY.
3. Скопируйте содержимое `lib/` в папку `lib/` на диске.
4. Подключите дисплей/кнопки/джойстик по распиновке из
   [midi-groovebox-docs/01-hardware.md](midi-groovebox-docs/01-hardware.md).

## Документация

[`midi-groovebox-docs/README.md`](midi-groovebox-docs/README.md) — оглавление.
Рекомендуемый порядок чтения: обзор → аппаратура → конвейер обработки ноты →
структуры данных → меню → ввод → матрица режимов → роадмап.

## Лицензия

[MIT](LICENSE)
