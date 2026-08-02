# Структуры данных

Всё хранится в ОЗУ (RP2040, 264KB SRAM) — 16 слотов паттернов одновременно активны
в памяти. Сохранение на flash/ПЗУ — будущая доработка (см. `07-roadmap-open-questions.md`);
структуры ниже спроектированы так, чтобы их можно было напрямую сериализовать
(например в JSON или compact binary) без переработки схемы.

## Step (шаг паттерна)

```python
step = {
    "notes": [],            # список MIDI note number (0-127); пусто = rest
                              # notes[0] = root (для аккорда — база для chord builder)
    "active": bool,          # есть ли звучащая нота на этом шаге
    "tie": bool,              # True = продлить предыдущую ноту через этот шаг
                              # (не создаёт новый Note On) — см. Rest+note комбинацию
    "length_steps": int,      # номинальная длина ноты в шагах/16 (до применения ADSR)
}
```

## Pattern (паттерн)

```python
pattern = {
    "steps": [step, ...],          # длина списка = 16 / 32 / 48 / 64
    "length": int,                  # текущая используемая длина (может быть < len(steps))
    "bpm": int,
    "time_signature": (4, 4),       # числитель/знаменатель

    "key_filter": {
        "enabled": bool,             # по умолчанию False (Quick-строка Key: Scale = Off, 02.08.2026)
        "root_note": int,           # 0-11 (C..B)
        "scale": str,                # "off" по умолчанию; иначе "major", "minor", "dorian", ...
        "mode": str,                 # "snap_up" | "snap_down" | "mute"
    },

    "chord_cfg": {
        "enabled": bool,
        "type": str,                 # "triad" | "seventh" | "sus2" | "sus4" | ... (см. ниже)
        "voicing": str,               # "block" | "strum" | "roll" (манера игры)
        "strum_delay_ms": int,        # используется если voicing == "strum"
    },

    "arp_cfg": {
        "enabled": bool,
        "latch": bool,               # True = одно нажатие держит арпеджио/ноту,
                                       # повторное нажатие той же ноты отпускает
                                       # (глобальный тумблер, см. 07-roadmap-open-questions.md)
        "rate": {"unit": "note" | "ms", "value": str | int},
                # unit="note": value из ["1/64".."1/1"] (квант по длительности ноты,
                #   по умолчанию "1/8", 02.08.2026);
                # unit="ms": value int 10..2000 (точный интервал)
                # см. mode_engine._arp_interval_ms()
                # В меню единый пункт "Rate" (объединяет бывшие RateDiv/RateMs,
                # 02.08.2026) + переключатель "RateMode" (note/ms);
                # в режиме ms изменение кратно 10 мс.
        "range_semitones": int,   # по умолчанию 12 (одна октава), 02.08.2026 —
                                  # при 0 одиночная нота не арпеджируется (все стили
                                  # звучат одинаково); задаёт гуляние за пределы набора
        "num_steps": int,
        "style": str,                 # "up" | "down" | "up_down" | "down_up" |
                                       # "as_played" | "random" | "converge_diverge"
    },

    "timing_cfg": {
        "swing_pct": int,             # 0-100 (0 = нет свинга)
        "humanize_amount_ms": int,
        "quantize_grid": str,          # "1/16" | "1/8T" | ... (кратность шагу)
        "legato": bool,
    },

    "gate_cfg": {
        "enabled": bool,
        "attack_ms": int,       # до 500 мс
        "decay_ms": int,         # зарезервировано, в UI; в гейт-модель пока не влияет
        "sustain_pct": int,      # 0-100, уровень удержания (заготовка)
        "release_ms": int,       # до 500 мс
        "sync": "ms" | "quantize",   # единицы для attack/decay/release
        "quantize_den": int,     # знаменатель сетки (16 = 1/16), если sync == quantize
    },

    "transpose": {
        "semitones": int,
        "octaves": int,
    },

    # Применяется к сырой ноте ДО key filter (см. 02-midi-chain.md).
    # Октава нотных клавиш (OctUp/OctDn) живёт отдельно: runtime.base_octave,
    # и добавляется к ноте тем же образом на входе конвейера.

    "random_cfg": {                    # см. подробности ниже
        "note_length": {"mode": str, "min": int, "max": int, "unit": str},
        "note_range": {
            "mode": "simple" | "detailed",
            "proximity": "close" | "medium" | "far",   # для simple: ±5 / ±12 / ±24 полутона
            "manual_min": int, "manual_max": int,        # для detailed
        },
        "density_or_probability": float,   # 0.0-1.0
        "shape": str,                       # "ascending" | "descending" | "arch" | ...
        "velocity_mode": {
            "mode": "off" | "range" | "delta",
            "off_value": int,                # фикс. 100 из 127 (хранится для совместимости
                                              # с MIDI-спекой, даже если фактически не
                                              # используется как "динамика нажатия")
            "min": int, "max": int,           # для mode == "range"
            "delta": int,                      # для mode == "delta"
        },
    },
}
```

## Slot (слот хранения)

```python
slots = [pattern_0, pattern_1, ..., pattern_15]   # ровно 16, фиксированный размер
current_slot = 0                                   # индекс активного паттерна
```

Один активный паттерн одновременно (без многодорожечности — зафиксировано с
заказчиком). Переключение слота = замена `current_slot`, воспроизведение при
переключении **не останавливается** (решение зафиксировано 30.07.2026).
Каждый слот хранит свой паттерн, BPM, time_signature и всю цепочку конфигов
(key_filter, chord_cfg, arp_cfg, timing_cfg, gate_cfg, transpose, random_cfg).

### Одноразовый флаг `_rec_used`

При записи в realtime-режиме на шаг паттерна ставится служебный флаг
`"_rec_used": True` после того, как в него записана нота. Это предотвращает
многократную перезапись одного и того же шага живым вводом. Флаг не
сериализуется и сбрасывается при повторном входе в запись.

## Runtime state (не сохраняется в паттерн, состояние выполнения)

```python
runtime = {
    "mode": str,                # "midi_keyboard" | "pattern" | "random_pattern" |
                                  # "random_note" | "midi_filter" — см. 06-mode-matrix.md
    "playing": bool,
    "recording": bool,
    "current_step": int,
    "screen_mode": str,           # "quick" | "full" | "animation"
    "menu_stack": [],             # см. 04-menu-navigation.md
    "live_mute": bool,             # состояние удержания Rest во время playback
    "base_octave": int,            # октава нотных клавиш (по умолчанию 4);
                                     # меняется OctUp/OctDn, отображается в статус-баре
}
```

## Список типов аккордов (для `chord_cfg.type`)

Полный список из ТЗ, для справки при реализации chord builder-а (интервалы от root,
в полутонах):

- **Triads**: major (0,4,7), minor (0,3,7), diminished (0,3,6), augmented (0,4,8)
- **Seventh chords**: maj7, min7, dom7, min7b5, dim7
- **Extensions**: 9, 11, 13 (и их major/minor варианты)
- **Sus**: sus2, sus4
- **Altered**: 7♯5, 7♯9, 7♭9, 7♯11 и т.п.
- **Sus + 7 комбинации**: sus2/7 (напр. Csus2/7), 7sus4
- **Slash chords**: аккорд/бас (например C/G) — отдельное поле `bass_note`,
  не входящее в саму интервальную структуру, а добавляемое поверх при выводе на MIDI
- **Quartal/Quintal harmony** — аккорды на кварто/квинтовых интервалах (0,5,10 / 0,7,14)
- **Cluster** — соседние полутона (0,1,2...) — диссонирующий кластер
- **Power chord (5)** — (0,7) без терции

Согласно уточнению — радиальный селектор джойстика ограничен **8 вариантами**
(7 типов + 1 "выключено/хроматика"), полный список выше доступен только в полном
меню (Full Menu) как расширенный выбор, не привязанный к джойстику напрямую.
