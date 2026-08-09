# Открытые вопросы и дорожная карта

Статус: **alpha** (C++17 / Pico SDK). Основной функционал live-игры, timing-
эффекты, gate/ADSR (attack/release) и voicing Strum/Roll реализованы;
паттерн-секвенсор — сознательно отложен.

## Что реализовано в C++-релизе (alpha)

- статическая типизированная модель состояния (`types.hpp`, 16 слотов в ОЗУ);
- чтение каскада 74HC165: двойной опрос + дебаунс нот и функц. кнопок;
- джойстик KY-023: 8 направлений, клик, long-press, авто-повтор (~220 мс);
- драйвер SH1106 (I2C, framebuffer + flush, инкрементальный рендер);
- Key Filter (16 ладов) + Chord Builder (26 типов) + secondary filter;
- полифонический live-арпеджио (19 стилей + Chord trigger, rate note/ms с
  триолями, колонки keys×range, фильтр по тональности, steps, latch);
- RandomNote — непрерывный генератор случайных нот (якорь по клавише/Play);
- Quick/DETAIL/MAIN/Animation меню; сброс Rest+клик; двойной клик в DETAIL/MAIN;
- полное дерево Full меню (10 разделов, см. `04-menu-navigation.md`);
- live-применение timing-эффектов (swing/humanize/quantize/legato), Gate/ADSR
  (attack/release через очередь отложенных событий) и voicing Block/Strum/Roll
  (см. `02-midi-chain.md`, этапы 5–8);
- клик-настройки (Debounce/Click 2x/Click Lng) хранятся во flash (magic `DROM`,
  версия 1, CRC32; `persist.cpp`);
- экран Test (диагностика входа, выход Shift+клик);
- USB MIDI Note On/Off (velocity 100) + CC-дубли функц. кнопок (CC20–25, канал 16);
- C++ сборка: `firmware-cpp-alpha/CMakeLists.txt`, Pico SDK 1.5.x, TinyUSB.

## Что открыто / отложено (зафиксировано с заказчиком)

| Область | Статус | Комментарий |
|---|---|---|
| Паттерн-секвенсор (запись/воспроизведения/step-edit) | ❌ отложено | нет playback, нет записи паттернов; Play в KB просто переключает флаг |
| RandomPattern-режим | ❌ отложено | требует секвенсора; enum зарезервирован |
| MIDI Filter (MIDI IN → OUT) | ❌ отложено | требует UART MIDI IN + оптрон |
| Timing-эффекты (swing/humanize/quantize/legato) | ✅ реализовано | применяются к live-арпедужо (этап 5 `02-midi-chain.md`) |
| Gate/ADSR (attack/release) | ✅ реализовано | очередь отложенных Note On/Off; опт decay/sustain зарезервированы |
| Voicing Strum/Roll в live-выводе | ✅ реализовано | Block/Strum/Roll по `strum_delay_ms` (этап 8) |
| UART MIDI DIN (OUT, затем IN) | ❌ план | см. `01-hardware.md` (выбор GP, 31250 бод, оптрон для IN) |
| MIDI Clock sync (master/slave) | ❌ отложено | BPM — внутренний генератор |
| Сохранение паттернов на flash | ❌ отложено | есть только клик-настройки; структуры сериализуемы |
| Velocity-чувствительный ввод | ❌ отложено | кнопки дают On/Off |
| Энкодер | ❌ отложено | весь ввод через джойстик |
| CC-дубли функц. кнопок | ✅ реализовано | CC20–25, канал 16, 127/0 (вне Test) |
| Экран Test | ✅ реализовано | System → Test, выход Shift+клик |

## Замечания по точной распиновке (закрыто 08.08.2026)

Финальная карта chip3: Play=bit0, Rest=bit1, Rec=bit2, Shift=bit3,
OctUp=bit6 («+»), OctDown=bit7 («−»); биты 4–5 не подключены (маскируются).
Промежуточная правка (octave на bit5/bit6) была ошибочной — см. `01-hardware.md`.

## Известный аппаратный баг

Перепутанные провода на втором 74HC165 (D4–D7): кнопка №13 на D7 вместо D4.
Оставлено как есть — физическая разводка настроена (см. `01-hardware.md`).
Программный remap не применять.

## Порядок дальнейшей работы (план)

1. Паттерн-секвенсор: Step-модель и планировщик воспроизведения (структуры готовы).
2. Реализация записи (realtime overdub, step-edit) в паттерн-режиме.
3. ~~Применение timing-эффектов (swing/humanize/quantize/legato)~~ ✅
4. ~~Gate/ADSR как модель тайминга (attack/release)~~ ✅
5. ~~Live-вывод Strum/Roll с детерминированным таймингом~~ ✅
6. Режимы RandomPattern/MidiFilter по мере появления секвенсора и MIDI IN.
7. UART MIDI OUT → MIDI IN + оптрон → midi_filter.
8. MIDI Clock sync (master/slave), клик-повторитель.
9. Сохранение паттернов на flash (сериализация struct-модели в выводу).
10. Возможный энкодер как дополнительный источник "increment/decrement".