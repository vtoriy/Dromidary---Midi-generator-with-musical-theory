# Документация проекта: MIDI Groovebox на RP2040 / CircuitPython

Читать в этом порядке:

1. [00-overview.md](00-overview.md) — что это, ключевые блоки, зафиксированные с
   заказчиком решения, известный баг, статус проекта.
2. [01-hardware.md](01-hardware.md) — распиновка, компоненты, битовая карта кнопок,
   план по MIDI UART.
3. [02-midi-chain.md](02-midi-chain.md) — конвейер обработки ноты (главная логика).
4. [03-data-structures.md](03-data-structures.md) — структуры данных паттерна/шага/
   слота и конфигов параметров.
5. [04-menu-navigation.md](04-menu-navigation.md) — состояние экрана, стек меню,
   тайминги long-press.
6. [05-input-mapping.md](05-input-mapping.md) — кнопки, джойстик, радиальный
   селектор аккордов/арпеджио, функционал кнопки Rest.
7. [06-mode-matrix.md](06-mode-matrix.md) — таблица доступности функций по режимам.
8. [07-roadmap-open-questions.md](07-roadmap-open-questions.md) — что отложено и
   что нужно уточнить.

Рабочий прототип ввода/дисплея (без MIDI-логики) — `../references/base_prototype.py`.
