# Документация проекта: MIDI Groovebox на RP2040 (C++ / Pico SDK)

Документация описывает **выпущенный C++-код** (`firmware-cpp-alpha/`, Pico SDK +
C++17/TinyUSB). Читать в этом порядке:

1. [00-overview.md](00-overview.md) — что это, ключевые блоки, зафиксированные с
   заказчиком решения, известный баг, статус проекта.
2. [01-hardware.md](01-hardware.md) — распиновка, битовая карта 74HC165, CC-дубли,
   экран Test, план по MIDI UART.
3. [02-midi-chain.md](02-midi-chain.md) — конвейер обработки ноты
   (transpose → key filter → chord → secondary filter → arp) и RandomNote.
4. [03-data-structures.md](03-data-structures.md) — C++-структуры паттерна/шага/
   слота, конфигов параметров, ClickSettings и persistence во flash.
5. [04-menu-navigation.md](04-menu-navigation.md) — экраны (Quick/DETAIL/MAIN/Animation),
   клик-циклы, радиальный селектор, полное дерево Full меню.
6. [05-input-mapping.md](05-input-mapping.md) — кнопки, джойстик, радиальные зоны,
   сброс значений, экран Test.
7. [06-mode-matrix.md](06-mode-matrix.md) — реализованные режимы (KB/RND) и
   зарезервированные.
8. [07-roadmap-open-questions.md](07-roadmap-open-questions.md) — что реализовано
   в alpha и что отложено на будущее.

Сборка и прошивка: [`../firmware-cpp-alpha/README.md`](../firmware-cpp-alpha/README.md).