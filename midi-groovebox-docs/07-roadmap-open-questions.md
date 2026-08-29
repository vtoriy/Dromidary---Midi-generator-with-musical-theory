# Открытые вопросы и дорожная карта

Статус: **alpha** (C++17 / Pico SDK). Реализованы live-игра, timing-эффекты,
gate/ADSR (attack/release), voicing Strum/Roll, генератор случайных нот
(PITCH/LEN/REP + Len Chain + Triplets), MIDI Clock (master/slave);
паттерн-режимы развиваются (RandomPattern MVP).

## Что реализовано в C++-релизе (alpha)

- статическая типизированная модель состояния (`types.hpp`, 16 слотов в ОЗУ);
- чтение каскада 74HC165: двойной опрос + дебаунс нот и функц. кнопок;
- джойстик KY-023: 8 направлений, клик, long-press, авто-повтор с ускорением;
- драйвер SH1106 (I2C, framebuffer + flush, инкрементальный рендер);
- Key Filter (16 ладов) + Chord Builder (26 типов) + secondary filter;
- полифонический live-арпеджио (19 стилей + Chord trigger, Rate/Distance/Steps/
  Cycle, триоли в rate, фильтр по тональности, latch);
- Timing в QUICK (строка Time), BPM в `TimingCfg`;
- RandomNote — непрерывный генератор случайных нот: диапазон высоты PITCH
  (дефолт C1–B1), диапазон длины LEN с триолями (Timing → Triplets), REP — шанс
  повтора якорной ноты, Len Chain (цепочка длин vs сетка ARP Rate);
- переключатель режима KB/RND в заголовке QUICK; скринсейвер Animation
  (автозапуск по простою + ручной запуск System → Anim);
- Quick/DETAIL/MAIN/Animation меню; сброс Rest+клик; двойной клик в DETAIL/MAIN;
- полное дерево Full меню (10 разделов, см. `04-menu-navigation.md`);
- live-применение timing-эффектов (swing/humanize/quantize/legato), Gate/ADSR
  (attack/release через очередь отложенных событий) и voicing Block/Strum/Roll
  (см. `02-midi-chain.md`, этапы 5–8);
- MIDI Clock sync (Timing → Clock): Master — генерирует F8/FA/FB/FC и шлёт BPM;
  Slave — следует за хостом, BPM оценивается по интервалам F8;
- клик-настройки (Debounce/Click 2x/Click Lng/Idle Anim) хранятся во flash
  (magic `DROM`, версия 3, CRC32; `persist.cpp`);
- экран Test (диагностика входа, выход Shift+клик);
- USB MIDI Note On/Off (velocity 100) + CC-дубли функц. кнопок (CC20–25, канал 16);
- C++ сборка: `firmware-cpp-alpha/CMakeLists.txt`, Pico SDK 1.5.x, TinyUSB.

## Что открыто / отложено (зафиксировано с заказчиком)

| Область | Статус | Комментарий |
|---|---|---|
| Паттерн-режим Pattern (запись по Rec + playback + экран EDIT) | ✅ MVP | моно-шаги, сетка 1/16 (`grid64` → 1/64), длительности — деления LEN; запись клавиш + RND-потока |
| Запись в паттерн (realtime overdub, step-edit) | 🔶 частично | запись и базовый step-edit в EDIT готовы; overdub-наложение поверх звучащих шагов и copy/paste шагов — дальше |
| RandomPattern-режим (авто-генерация паттерна) | ✅ MVP | генерация в активный слот по PITCH/LEN/Dens/REP; playback — цепочка длительностей (каждое событие длится свою LEN-долю, onset = конец предыдущего гейта); см. `06-mode-matrix.md` |
| MIDI Filter (MIDI IN → OUT) | ❌ отложено | требует UART MIDI IN + оптрон |
| Timing-эффекты (swing/humanize/quantize/legato) | ✅ реализовано | применяются к live-арпеджио (этап 5 `02-midi-chain.md`) |
| Gate/ADSR (attack/release) | ✅ реализовано | очередь отложенных Note On/Off; опц decay/sustain зарезервированы |
| Voicing Strum/Roll в live-выводе | ✅ реализовано | Block/Strum/Roll по `strum_delay_ms` (этап 8) |
| UART MIDI DIN (OUT, затем IN) | ❌ план | см. `01-hardware.md` (выбор GP, 31250 бод, оптрон для IN) |
| MIDI Clock sync (master/slave) | ✅ реализовано | Timing → Clock: Master шлёт F8/FA/FB/FC; Slave следует за хостом |
| Сохранение паттернов на flash | ❌ отложено | есть только клик-настройки (v3); структуры сериализуемы |
| Velocity-чувствительный ввод | ❌ отложено | кнопки дают On/Off |
| Энкодер | ❌ отложено | весь ввод через джойстик |
| Shape (Asc/Desc/Arch/Rnd) | ❌ зарезервирован | хранится, к движку не подключён; при Asc — вопрос заворота на границе диапазона |
| Dens (Density) | ❌ зарезервирован | редактируется шагом 10 %, на поток RND не влияет |
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

1. ~~Применение timing-эффектов (swing/humanize/quantize/legato)~~ ✅
2. ~~Gate/ADSR как модель тайминга (attack/release)~~ ✅
3. ~~Live-вывод Strum/Roll с детерминированным таймингом~~ ✅
4. ~~MIDI Clock sync (master/slave)~~ ✅
5. ~~RandomPattern MVP (генерация + playback)~~ ✅
6. ~~Паттерн-режим Pattern: запись по Rec, playback, экран EDIT~~ ✅ (MVP)
7. Доработка редактора: overdub-наложение, copy/paste шагов, полифония.
8. UART MIDI OUT → MIDI IN + оптрон → режим MidiFilter.
9. Сохранение паттернов на flash (сериализация struct-модели).
10. Подключение Shape и Dens к движку генерации.
11. Возможный энкодер как дополнительный источник "increment/decrement".