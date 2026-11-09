# Статус предложений из игрового опыта

Этот файл отделяет реализованные изменения от направлений, которым нужен
отдельный дизайн. Список не означает, что каждое предложение исходной заметки
уже реализовано. Подробные контракты находятся в справочнике и отдельных руководствах.

| Направление | Реализовано | Что остаётся |
|---|---|---|
| Синтаксис | Ленивый ?:, постфиксные поля/элементы, битовые операции и присваивания, hex/binary/exponent/underscore, /* */, BOM | prefix ++/--, do/while, метки, switch expression |
| Типы | Проверки известных типов ветвей/операндов, сохранение typed-container ограничений | Полное выведение общих типов динамических выражений, int64/uint64, frozen, flow narrowing, is/as |
| Диагностика | Восстановление блоков/части пропущенных ;, подавление каскада, лимит вывода, SKR1000/1001/2001 | Полный каталог runtime-кодов, стек/локальные значения обычного запуска, универсальное восстановление |
| Предупреждения/LSP | Затенение встроенных имён, --deny-warnings, quick fix переименования | unused/unreachable/fallthrough, отключение по коду, inlay hints/semantic tokens |
| Строки/UI | Unicode 16 UAX #29, байты/кодовые точки/графемы, редактирование по графемам | Полноценный IME и выделение произвольного диапазона |
| Шрифты | Проверка покрытия, missing clusters, fallback TTF, load_font_file, новые bitmap-символы | HarfBuzz shaping/kerning/bidi, системный fallback, COLR/CPAL/CBDT/sbix |
| Цветные игровые иконки | PNG-атлас, draw_icon, трансформации спрайтов | Автоматическое сопоставление emoji-последовательностей с атласом, готовый лицензированный emoji-набор |
| Графика | Alpha кругов/линий/колец/bitmap/TTF, полигоны/треугольники/эллипсы/градиенты, RGB canvas, PNG, 9-slice, batches | RGBA render targets, additive blend, arcs/Bezier, иконка окна, GPU/vsync |
| Окно/ввод | FPS limiter; курсор, захват, relative mouse Windows/X11 | Геймпад, multitouch, input_map; pointer lock Web/Android |
| Звук | Микшер с handles, 3 каналами, gain/pan/loop/pause/fade, streaming WAV/MP3/FLAC/Vorbis; Windows legacy overlap | Static-musl device backend, Web audio, waveform/envelope synthesis |
| Стандартная библиотека | seed/shuffle, unique/chunk/flatten, assert_close, clock_ms64, math helpers, user_data_dir | binary_search; самостоятельный Random с независимым seed |
| Игровые помощники | Vec2/Rect/Circle, коллизии, камера follow/transforms, Timer/Interval/Tween, pool частиц, атомарный JSON | Camera shake, расширенные easing/тайлмапы/готовый движок |
| Проекты | init, полноценный TOML parser, entry/resources/lint, build --force, check проекта, явный script test | Зависимости/пакетный менеджер, watch, REPL, hot reload, форматтер-конфигурация |
| Тесты | Новые runtime/CLI/LSP/renderer/mixer/Unicode тесты | Параметризация, fixture hooks, coverage |
| Документация | Шрифты, звук, проекты, игры, отличия языка, английский обзор | Полный перевод, сайт справочника с поиском |

Некоторые пункты уже существовали до обновления: `get_or` для безопасного чтения,
`obj?.method()`, глубокая `copy`, aliases модулей. Они документированы, а не
представлены как новые возможности.

Измерение шрифта учитывает fallback и advances, но не shaping. Проверка
text_supported консервативно отклоняет составные графемы, поэтому её нельзя
использовать как доказательство отсутствия отдельных глифов.

## Проверки

- Официальный GraphemeBreakTest Unicode 16: 1093 случая.
- Offline PCM mixer: сумма двух голосов, mute/pause/resume/fade.
- Pixel tests: alpha, clipping, rotation/flip, PNG roundtrip, canvas lifetime.
- Native window: limiter, курсор/захват при доступном фокусе, графемное удаление
  и последовательные combining events.
- CLI: init без перезаписи, проектная сборка/force с защитой исходника, BOM,
  malformed literals, parser recovery, warning exit policy, fmt idempotence.
- LSP: quick fix затенённого параметра изменяет объявление и его ссылки.

Локальные проверки выполнены на Linux/X11. Реальные Windows/Android устройства,
цветные шрифты и аппаратная задержка аудио требуют отдельных измерений;
платформенные сборки проверяются CI. Файл moon_forest.skm не был предоставлен
и в репозитории отсутствует, поэтому проверка именно этой игры не заявляется.
