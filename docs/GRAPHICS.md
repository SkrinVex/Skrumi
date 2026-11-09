# Нативная графика Skrumi

[Визуальный конструктор Skrumi UI](https://skrinvex.github.io/Skrumi/builder/) создаёт
интерфейсы на этом API с макетами ПК/телефона и живым запуском. [Руководство](BUILDER.md).

Модуль `graphics` открывает обычное окно операционной системы. Рисование и игровая
логика выполняются внутри Skrumi runtime; браузер, JavaScript, OpenGL и отдельные
графические ресурсы не нужны.

```cpp
using graphics;

open_window(800, 600, "Моя игра");
while (window_poll()) {
    if (key_pressed("ESCAPE")) { break; }
    clear_window(rgb(12, 20, 35));
    draw_rect(30, 40, 180, 50, rgb(255, 146, 86));
    draw_circle(mouse_x(), mouse_y(), 20, rgb(121, 241, 204));
    draw_text(30, 120, "Привет, Skrumi!", 3, rgb(240, 245, 255));
    present_window();
    wait(16);
}
close_window();
```

Запуск и упаковка обычные:

```bash
skrumi game.skm
skrumi build game.skm -o game
```

Получателю достаточно готового executable и графического сеанса ОС. На Linux
это X11 или XWayland с корректным `DISPLAY`; Wayland без XWayland пока не поддержан.
В [песочнице на сайте](https://skrinvex.github.io/Skrumi/playground/) окно рисуется
прямо на странице, так что игру можно показать без установки (пример «Игра в окне»).

Готовый кадр передаётся X-серверу через разделяемую память (MIT-SHM), если сервер
на том же компьютере; иначе — по сокету, как раньше. `SKRUMI_X11_SHM=0` выключает
разделяемую память, например для проверки второго способа.
На Windows используется Win32/GDI. Сервер без графического сеанса может запускать
обычные консольные Skrumi-программы: графическая система подключается только
при `open_window`.

На Android 9+ тот же код рисуется в `SkrumiView`: касания, экранная клавиатура,
автоматическое изменение доступного размера и системный fullscreen.
Сборка AAR/APK и правила координат описаны в [ANDROID.md](ANDROID.md).

## Размеры и полноэкранный режим

За рамку обычного окна можно потянуть мышью; максимизация также поддерживается.
`window_width()` и `window_height()` возвращают размер области рисования,
`window_resized()` сообщает об изменении в текущем кадре после `window_poll()`.
Приложение само перестраивает интерфейс под новый размер: фиксированные координаты
не масштабируются автоматически. При увеличении холста сохраняются старые пиксели,
новая область чёрная; при уменьшении обрезанная часть теряется. Paint закрашивает
новые полосы белым, а псевдо-3D пересчитывает число лучей и буфер глубины.

`set_window_fullscreen(true)` переводит окно в полноэкранный режим без смены
разрешения монитора. `set_window_fullscreen(false)` возвращает прежнее положение,
размер и возможность растягивания; `window_fullscreen()` сообщает состояние.
На Linux нужен оконный менеджер с EWMH (например, GNOME, KDE, Openbox), в том числе
через XWayland. Без него переход выдаёт ошибку. Подтверждение перехода на X11
приходит во время `window_poll()`, поэтому состояние и размеры проверяют в цикле.
`set_window_size` допустим в обычном режиме: сначала выйдите из fullscreen и
дождитесь `window_fullscreen() == false`. Фиксированное окно тоже может переходить
в fullscreen. В браузерной песочнице запрос fullscreen через это API выдаёт ошибку:
полноэкранный доступ страницы требует отдельного пользовательского жеста.

```skrumi
using graphics;
open_window(800, 600, "Размеры и fullscreen");
while (window_poll()) {
    if (key_pressed("F11")) { set_window_fullscreen(!window_fullscreen()); }
    if (key_pressed("ESCAPE")) {
        if (window_fullscreen()) { set_window_fullscreen(false); }
        else { break; }
    }
    clear_window(rgb(20, 30, 40));
    draw_circle(window_width() / 2, window_height() / 2, 20, rgb(240, 120, 30));
    present_window();
    wait(16);
}
close_window();
```

Начальный размер и режим можно переопределить без изменения исходника:
`skrumi game.skm --skrumi-width 960 --skrumi-height 540 --skrumi-fullscreen`.
Windows GUI EXE без терминала: `skrumi build game.skm --no-console`.
Все флаги запуска и правила пользовательских аргументов — [LAUNCH.md](LAUNCH.md).

## API

| Функция | Результат и назначение |
|---|---|
| `open_window(int width, int height, string title)` | Открыть окно с заданным размером области рисования |
| `window_poll()` | `bool`: обработать события, вернуть `false` после закрытия пользователем |
| `close_window()` | Освободить окно; повторный вызов допустим |
| `rgb(int red, int green, int blue)` | `int`: цвет; каждый канал 0..255 |
| `clear_window(int color)` | Очистить буфер кадра |
| `draw_rect(int x, int y, int width, int height, int color)` | Заполненный прямоугольник |
| `draw_circle(int x, int y, int radius, int color)` | Заполненный круг |
| `draw_text(int x, int y, string text, int scale, int color)` | Текст встроенным пиксельным шрифтом |
| `present_window()` | Показать собранный кадр |
| `frame_delta()` | `float`: секунды между вызовами `window_poll`, от 0 до 0.1 |
| `key_down(string key)` | `bool`: удерживается ли клавиша/кнопка |
| `key_pressed(string key)` | `bool`: новое нажатие в текущем кадре |
| `mouse_x()`, `mouse_y()` | `int`: координаты указателя относительно окна |
| `set_window_fullscreen(bool fullscreen)` | Включить/выключить нативный fullscreen, восстановить обычное окно при выходе |
| `window_fullscreen()` | `bool`: текущее состояние fullscreen; на X11 подтверждается в `window_poll()` |
| `window_focused()` | `bool`: имеет ли окно фокус клавиатуры |
| `draw_line(int x1, int y1, int x2, int y2, int color)` | Отрезок толщиной в пиксель |
| `draw_frame(int x, int y, int width, int height, int thickness, int color)` | Контур прямоугольника |
| `draw_ring(int x, int y, int radius, int thickness, int color)` | Окружность заданной толщины |
| `text_width(string text, int scale)` | `int`: ширина самой длинной строки текста в пикселях; окно не нужно |

Каждая функция модуля — обёртка над встроенной функцией с префиксом `gfx_`
(`open_window` → `gfx_open`, `draw_text` → `gfx_text` и т. д.); их можно вызывать
и без `using graphics;`. Готовый пример с движением и управлением стрелками —
[examples/bouncing_ball.skm](../examples/bouncing_ball.skm).

Текст по центру удобно выравнивать через `text_width`:

```cpp
using graphics;
int scale = 3;
string title = "Пауза";
open_window(400, 200, "Центр");
clear_window(rgb(0, 0, 0));
draw_text((400 - text_width(title, scale)) / 2, 90, title, scale, rgb(255, 255, 255));
draw_frame(10, 10, 380, 180, 2, rgb(255, 140, 0));
draw_line(10, 70, 390, 70, rgb(80, 80, 80));
present_window();
close_window();
```

## Ввод текста, мышь и буфер обмена

| Функция | Результат и назначение |
|---|---|
| `text_input()` | `string`: символы, набранные с прошлого кадра, с учётом раскладки (в том числе русской), Shift, Caps Lock и автоповтора. Управляющие клавиши (Enter, Tab, Backspace) в текст не входят |
| `key_repeat(string key)` | `bool`: клавиша нажата в этом кадре **или** повторяется системой при удержании — для Backspace, стрелок, прокрутки списка |
| `mouse_wheel()` | `int`: щелчки колеса с прошлого кадра; больше нуля — от себя |
| `double_clicked()` | `bool`: двойной щелчок левой кнопкой в этом кадре |
| `clipboard_text()`, `set_clipboard_text(string text)` | системный буфер обмена |
| `draw_rect_alpha(int x, int y, int width, int height, int color, int alpha)` | полупрозрачный прямоугольник, alpha 0..255 |
| `window_width()`, `window_height()` | размер области рисования |
| `clip_begin(int x, int y, int width, int height)`, `clip_end()` | рисовать только внутри прямоугольника (вложенные сужают область); сбрасывается в начале кадра, `clear_window` заливает всё окно |
| `set_window_resizable(bool resizable)`, `set_window_size(int width, int height)`, `window_resized()` | изменяемый размер окна: область рисования следует за окном (64..4096 по каждой оси) |

## Картинки

| Функция | Результат и назначение |
|---|---|
| `load_image(string path)` | `int`: номер картинки PNG или BMP; окно не нужно |
| `image_width(int image)`, `image_height(int image)` | размер в пикселях |
| `draw_image(int image, int x, int y)` | рисует картинку с её прозрачностью |
| `draw_image_scaled(int image, int x, int y, int width, int height)` | растягивает по ближайшему пикселю |
| `draw_image_alpha(int image, int x, int y, int opacity)` | вся картинка полупрозрачна, 0..255 |
| `draw_image_part(int image, int source_x, int source_y, int source_width, int source_height, int x, int y, int width, int height)` | часть картинки: кадр спрайта, плитка атласа |
| `image_pixel(int image, int x, int y)`, `image_alpha(int image, int x, int y)` | цвет `rgb(...)` и непрозрачность пикселя |
| `free_image(int image)` | освобождает картинку |
| `draw_sprites(int image, int frame_width, int frame_height, array sprites)` | много кадров атласа за вызов: `sprites` — тройки «номер кадра, x, y»; кадры нумеруются слева направо и сверху вниз с 0 |
| `draw_tiles(int image, int tile_width, int tile_height, array tiles, int columns, int x, int y)` | карта плиток: `tiles` — номера плиток по строкам, `columns` в строке, угол карты в (x, y); номер меньше 0 — пустая клетка; рисуются только видимые плитки |
| `draw_rects(array rects)` | много прямоугольников за вызов: пятёрки «x, y, ширина, высота, цвет» |

Для игр с большим числом объектов пакетные функции заметно быстрее цикла из
`draw_image_part`: один вызов из программы вместо сотен, а плитки за краем окна
не перебираются вовсе.

```cpp
using graphics;
int atlas = load_image("tiles.png");          // атлас плиток 16×16
array level = [0, 0, 1, 1, -1, 2, 2, 2];         // -1 — пусто
open_window(320, 240, "Карта");
while (window_poll()) {
    clear_window(rgb(0, 0, 0));
    draw_tiles(atlas, 16, 16, level, 4, 0, 0);   // 4 плитки в строке
    draw_sprites(atlas, 16, 16, [3, 100, 50, 3, 140, 50]);
    present_window();
}
```

PNG поддерживается целиком: серые, цветные и палитровые изображения, 1–16 бит на
канал, прозрачность (альфа-канал и `tRNS`), чересстрочная развёртка Adam7. BMP —
несжатый 24- и 32-битный. Декодер встроен в Skrumi, внешних библиотек нет;
повреждённый файл — ошибка `Graphics Error`, её можно перехватить `try`.

Готовые кнопки, поля ввода с курсором, прокручиваемые области и модальные окна, которые правильно
распределяют щелчки и клавиатуру, — в модуле `ui` (см.
[DOCUMENTATION.md](../DOCUMENTATION.md#интерфейс-кнопки-поля-диалоги)). Проверять
щелчки через `key_pressed("MOUSE_LEFT")` и координаты мыши стоит только там, где нет
перекрывающихся элементов: такой щелчок видят все части окна, в том числе закрытые
диалогом.

Клавиши: `A`–`Z`, `0`–`9`, `LEFT`, `RIGHT`, `UP`, `DOWN`, `SPACE`, `ENTER`,
`ESCAPE`, `TAB`, `BACKSPACE`, `DELETE`, `INSERT`, `HOME`, `END`, `PAGE_UP`,
`PAGE_DOWN`, `F1`–`F12`, `SHIFT`, `CTRL`, `ALT`, `MOUSE_LEFT`, `MOUSE_RIGHT`, `MOUSE_MIDDLE`. Однобуквенные имена
допускают нижний регистр. Неизвестное имя вызывает понятную ошибку.

Имена клавиш не зависят от раскладки: `key_down("W")` работает и при активной
русской раскладке, потому что латинский keysym ищется во всех группах раскладки.

Вызывайте `window_poll()` один раз в начале кадра. Он сбрасывает прежние события
нажатия, обновляет ввод и время кадра. Повторное чтение `key_pressed()` в одном
кадре возвращает то же значение. Удержание клавиши не должно трактоваться как
новое нажатие. Потеря фокуса сбрасывает удерживаемые клавиши; приложение само
может поставить игру на паузу по `window_focused()`.

`present_window()` не ограничивает частоту кадров и не включает вертикальную
синхронизацию. Используйте `set_target_fps(60)` для ограничения нагрузки, а `frame_delta()`
для движения независимо от частоты кадров. Для быстрых объектов разбивайте
длительный кадр на несколько шагов физики.

На Android `window_poll()` дополнительно уступает поток на 16 мс, а `SkrumiView`
масштабирует программный кадр в Activity. Отдельные затраты JNI, Bitmap и вызова
рисования можно [измерить системной трассировкой](ANDROID_PERFORMANCE.md), не
смешивая их со временем VM и расчётом сцены.

## Координаты, шрифт и ограничения

Начало координат — левый верхний угол, X направлена вправо, Y вниз. Координаты
и размеры — пиксели. Фигуры обрезаются по границам буфера и по `clip_begin`. Размер прямоугольника
не может быть отрицательным; радиус круга — 0..8192. Цвет — целое `0xRRGGBB`
(в Skrumi удобно получать его через `rgb`). Альфа 0..255 поддерживается у `draw_rect_alpha`, `draw_circle_alpha`,
`draw_line_alpha`, `draw_ring_alpha`, `draw_text_alpha` и изображений.

Встроенный шрифт 5×7 (у `g p q y j д ц щ` — спуск ещё на две строки ниже, в пределах межстрочного интервала 9 пикселей) содержит латинский и русский алфавиты, цифры, знаки
препинания и символы `% " ' * # < > @ & { } | $ ^ ~ \ № ° « » — × …`, стрелки и
сердечко, `• ★ ⚡ ❤ ☠ ⬆ ⬇`. Строчные и прописные буквы различаются (латиница и кириллица), неизвестный символ — `?`.
`\n` переводит строку. Масштаб — целое 1..32, строка — до 65536 байт UTF-8.
`draw_text` не использует системные шрифты. Для TTF/OTF есть отдельные
`load_font`, `load_font_file` и `draw_font_text`, с fallback через
`font_with_fallback`. Цветные эмодзи и shaping не поддерживаются.
См. [шрифты, графемы и эмодзи](FONTS.md).

Одновременно поддерживается одно окно на интерпретатор. Чтобы открыть другое,
сначала закройте предыдущее. Размеры — 64..4096 по каждой оси, суммарно не более
8388608 пикселей. Нативные окна изменяют размер мышью по умолчанию;
`set_window_resizable(false)` запрещает это. Нативный Wayland и аппаратное 3D
не поддерживаются; звук — в модуле `sound`. API рассчитан на поток интерпретатора.

Окно принадлежит экземпляру интерпретатора и освобождается при `reset()` или
уничтожении интерпретатора. Ошибки открытия окна или аргументов возвращаются
как обычные ошибки runtime, без запуска внешних программ.

## Сборка и standalone

Для сборки Skrumi на Linux нужны заголовки XCB и pkg-config. Например, на
Debian/Ubuntu:

```bash
sudo apt-get install pkg-config libxcb1-dev libxau-dev libxdmcp-dev
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

Обычная локальная Linux-сборка связывается с системной libxcb. Docker target
`portable` включает XCB и её зависимости **статически**, вместе с musl и остальным
runtime: отдельные `.so` у получателя не требуются. Наличие X11/XWayland всё равно
необходимо для показа окна. Лицензии библиотек входят в `--skrumi-licenses`.
Windows-сборке достаточно системных `user32` и `gdi32`, дополнительных DLL Skrumi нет.
Для статической Linux-сборки CMake также загружает официальный libXau 1.0.12
с проверкой SHA-256: Alpine не предоставляет `libXau.a`. Для сборки без сети
задайте `FETCHCONTENT_SOURCE_DIR_XAU`, указав распакованный архив этой версии.

Модуль `graphics` встраивается в общую stdlib. Упаковщик использует тот же runtime,
поэтому `skrumi build` не требует дополнительных SDK или компилятора. Игра сама
может сохранять рекорды через `write_file`; такие данные остаются внешними файлами
и не упаковываются автоматически.

## Проверки

`unit_graphics` проверяет отсечение фигур, граничные координаты, текст UTF-8,
размеры буфера и ошибки API без открытия окна. `standalone_graphics` создаёт
настоящее окно из executable в чистом каталоге с пустым PATH, проверяет пиксели,
клавиатуру, мышь и закрытие через оконную систему.

Linux без рабочего стола может запускать тест через Xvfb:

```bash
sudo apt-get install xvfb libx11-6 openbox
SKRUMI_REQUIRE_GRAPHICS_TESTS=1 xvfb-run -a ctest --test-dir build -R graphics --output-on-failure
```

Без DISPLAY оконный тест локально пропускается; `SKRUMI_REQUIRE_GRAPHICS_TESTS=1`
делает отсутствие экрана ошибкой. CI использует Xvfb на Linux и Win32 на Windows.
Xlib применяется только тестовым драйвером Linux; само приложение использует XCB.

Описание системных API: [XCB](https://xcb.freedesktop.org/tutorial/),
[Win32 CreateWindowExW](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-createwindowexw),
[GDI StretchDIBits](https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-stretchdibits).

## Подсказки в редакторе

LSP предлагает модуль `graphics`, сигнатуры и русские описания всех его функций. После `using graphics;` начните вводить `draw_` или наведите
курсор на `open_window`. При `(` и `,` появляется справка по аргументам.
Обновите `skrumi-lsp` вместе с редакторным пакетом; см. [настройку редакторов](EDITORS.md).

## Canvas и преобразования

`create_canvas(width,height)` создаёт отдельную RGB-поверхность, в которую можно
рисовать без окна после `draw_to_canvas(handle)`. Возврат в окно —
`draw_to_canvas(0)`. `draw_canvas` копирует её в текущую цель, `canvas_png`
возвращает PNG-байты. Освобождайте поверхности через `free_canvas`.
Это непрозрачные поверхности, не слои с сохранённым alpha-каналом.

`draw_image_ex` и `draw_image_part_ex` добавляют масштаб, поворот вокруг центра,
отражение по осям и alpha. Угол в радианах, положительный — по часовой стрелке.
`draw_icon` выбирает ячейку атласа; `draw_image_nine_slice` масштабирует панели.
Есть `draw_triangle`, `draw_polygon`, `draw_ellipse`, `draw_gradient`, пакетные
`draw_circles`/`draw_lines`, `draw_font_text_alpha`. Полные сигнатуры — в справочнике.

`save_screenshot(path)` сохраняет PNG содержимого окна без системной рамки.
Обёртки `draw_text_aligned`, `draw_text_wrapped`, `wrap_bitmap_text` и
`text_ellipsis` упрощают подписи; перенос и обрезка сохраняют графемы целиком.
Это не добавляет shaping и не превращает отсутствующие глифы в цветные эмодзи.
