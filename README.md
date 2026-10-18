# <img src="resources/branding/skrumi-mark.svg" width="40" height="40" alt=""> Skrumi

Единственные команда и расширение исходников — `skrumi` и `.skm`.
[Перенос старых проектов](docs/MIGRATION.md).

Новые компоненты UI: [ProgressBar, диалоги, темы и отмена закрытия](examples/progress.skm).
Публичные файловые операции и их гарантии описаны в [документации](DOCUMENTATION.md#проверяемые-файловые-операции).
GUI-установщик: [установка, обновление и восстановление](docs/GUI_INSTALLER.md).


[![Версия исходников](resources/version.svg)](VERSION)
![C++](https://img.shields.io/badge/runtime-C%2B%2B17-blue)
![Платформы](https://img.shields.io/badge/platform-Linux%20%7C%20Windows%20%7C%20Android-lightgrey)
![Лицензия](https://img.shields.io/badge/license-MIT-green)
[![Песочница](https://img.shields.io/badge/%D0%BF%D0%B5%D1%81%D0%BE%D1%87%D0%BD%D0%B8%D1%86%D0%B0-%D0%BF%D0%BE%D0%BF%D1%80%D0%BE%D0%B1%D0%BE%D0%B2%D0%B0%D1%82%D1%8C%20%D0%B2%20%D0%B1%D1%80%D0%B0%D1%83%D0%B7%D0%B5%D1%80%D0%B5-d9620f)](https://skrinvex.github.io/Skrumi/playground/)

**Skrumi** — встраиваемый интерпретируемый язык общего назначения с понятным
C-подобным синтаксисом, строгими типами, массивами, модулями и стандартной
библиотекой для строк, файлов, JSON, HTTP, серверов и 2D-графики. Программу можно
запустить интерпретатором или собрать в один исполняемый файл, которому не нужен
установленный Skrumi.

**Android:** [встроить язык в Kotlin/Java или собрать APK из `.skm`](docs/ANDROID.md).
Демо игры: [raycaster APK](https://github.com/SkrinVex/Skrumi/releases/latest/download/skrumi-android-raycaster.apk) · [исходник](examples/raycaster.skm) · [замеры кадров на устройстве](docs/ANDROID_PERFORMANCE.md).

**Визуально собрать интерфейс:** [конструктор Skrumi UI](https://skrinvex.github.io/Skrumi/builder/) —
макеты для ПК и телефона, живой запуск, отдельные `layout.skm` и `main.skm`: правки макета не стирают вашу логику. [Руководство](docs/BUILDER.md).

**Попробовать без установки:** [песочница в браузере](https://skrinvex.github.io/Skrumi/playground/):
пишешь код и сразу запускаешь, даже с телефона. Там же есть примеры, в том числе игры
с окном. Как она устроена, рассказано на [сайте](https://skrinvex.github.io/Skrumi/#playground).

```cpp
using string;
using arrays;

array words = split("лис волк заяц", " ");
sort(words);
for (int i = 0; i < size(words); i++) {
    print(i + 1, upper(words[i]));
}
```

## Возможности

- Типы `int`, `float`, `string`, `bool`, `array`; одно понятное правило приведения
  типов для переменных, параметров и результатов функций.
- Функции с рекурсией, блочные области видимости, `if`/`while`/`for`/`switch`.
- Динамические массивы: литералы `[1, 2, 3]`, индексы `items[i]`, `push`/`pop`,
  сортировка и срезы; функции принимают и возвращают массивы.
- Словари `map` (`{"name": "Лис"}`, `m["key"]`, `m.key`) и структуры `struct` с
  типизированными полями; вложенные контейнеры (`users[0].tags[1]`), сравнение по
  содержимому, `json_decode` превращает JSON в словари и массивы.
- Обработка ошибок: `try` / `catch` / `finally` и `throw`.
- Строки с полной поддержкой UTF-8: длина, подстроки и поиск считают символы,
  `upper`/`lower` понимают кириллицу.
- Стандартная библиотека из 17 модулей: `string`, `arrays`, `math`, `json`, `fs`,
  `os`, `time`, `env`, `log`, `http`, `server`, `net`, `terminal`, `graphics`, `ui`, `sound`, `resources`.
- Веб-сервер для сайтов и API: маршруты с параметрами (`/users/:id`), статические
  файлы, HTML-шаблоны, формы и загрузка файлов, cookies, редиректы, скачивание, CORS,
  HTTPS без reverse proxy. HTTP(S)-клиент со статусом ответа, TCP и DNS — на Linux и
  Windows и Android, без внешних утилит.
- Сборка JSON без склейки строк (`json_set`, `json_value`) и чтение по путям.
- Нативное окно с 2D-рисованием, картинками PNG/BMP, изменяемым размером, звуком,
  текстом (строчные и прописные, латиница и кириллица),
  вводом текста в любой раскладке, мышью с колесом и буфером обмена; модуль `ui` с
  кнопками, полями ввода и модальными окнами, которые правильно распределяют щелчки
  и клавиатуру.
- Файловая система целиком: права доступа, время изменения, владелец, копирование и
  перемещение каталогов, работа с путями; запуск программ и открытие файлов
  приложением по умолчанию.
- `skrumi check` — статическая проверка без запуска; языковой сервер
  `skrumi-lsp` с расширениями для VS Code, Kate и Zed. Проект из нескольких файлов
  анализируется целиком, ошибки указывают файл и строку, программу можно запустить
  кнопкой ▶ прямо из редактора.
- Отладчик (Debug Adapter Protocol) для VS Code, Kate и Zed: точки останова с
  условиями и счётчиками, лог-точки, шаги, стек вызовов, переменные и массивы,
  изменение значений, консоль и остановка на ошибке выполнения.
- `skrumi test` — встроенные тесты с `assert` и `assert_equal`; `skrumi fmt` —
  единое оформление кода; в редакторе — поиск всех ссылок, переименование и
  форматирование.
- `skrumi build` — самостоятельное приложение для Linux или Windows.
- Библиотека `skrumi_core` для встраивания в программы на C++.

Полный справочник языка: [DOCUMENTATION.md](DOCUMENTATION.md). Как язык устроен
изнутри, простыми словами и с живыми примерами: [skrinvex.github.io/Skrumi](https://skrinvex.github.io/Skrumi/).

## Установка

Основной способ установки — самостоятельные графические установщики для
Windows и Linux x86_64. [Страница загрузки](https://skrinvex.github.io/Skrumi/#download)
показывает кнопки только для файлов, действительно опубликованных в последнем
стабильном [GitHub Release](https://github.com/SkrinVex/Skrumi/releases/latest).
До первого GUI-релиза пользуйтесь сборкой из исходников ниже.

Windows: запустите `Skrumi-windows-x86_64.exe`. Linux с X11/XWayland:
выдайте `Skrumi-linux-x86_64.run` право исполнения и запустите его.
Skrumi, Python, PowerShell и подключение к интернету для установки не требуются.
Установка выполняется для текущего пользователя; LSP и изменение PATH можно отключить.
Откройте новый терминал после установки и проверьте `skrumi --version`.

Для обновления запустите новый установщик с прежним каталогом. Для удаления
выберите соответствующий режим в установщике; Windows также регистрирует Skrumi
в списке установленных приложений. [Поставка, восстановление и ограничения](docs/GUI_INSTALLER.md).
Старые ZIP/TAR-пакеты остаются в исторических релизах, но новые релизы их не публикуют.

Сборка из исходников (CMake 3.18+, компиляторы C и C++17; на Linux ещё pkg-config
и `libxcb1-dev libxau-dev libxdmcp-dev`):

```bash
git clone https://github.com/SkrinVex/Skrumi.git
cd Skrumi
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
cmake --build build --config Release
cmake --install build --prefix ./install
```

Добавьте `install/bin` в `PATH` или запускайте `install/bin/skrumi` напрямую.

## Использование

```bash
skrumi program.skm [аргументы...]   # запуск
skrumi check program.skm            # проверка без запуска
skrumi build program.skm -o app     # самостоятельное приложение
skrumi build game.skm --no-console # GUI EXE без терминала в Windows
skrumi --version
skrumi help                         # все команды: запуск, проверка, сборка, тесты…
skrumi help build                   # подробно об одной команде
```

Параметры консоли, размера окна и fullscreen доступны и у готового приложения:
`app --skrumi-console --skrumi-width 960 --skrumi-height 540`.
Пользовательские флаги доступны через `os_args()`; `--` завершает разбор системных
флагов. Полные правила и примеры — [Запуск и параметры окна](docs/LAUNCH.md).


Код возврата: `0` — успех, `1` — ошибка (синтаксис, тип, выполнение, отсутствующий
модуль или секрет), либо значение, переданное в `exit(code)`.

### Самостоятельные приложения

```bash
skrumi build hello.skm -o hello
./hello          # Linux
.\hello.exe      # Windows
```

Внутри одного исполняемого файла — рантайм Skrumi, программа и все её модули.
Получателю не нужны Skrumi, компилятор или CMake. Это упаковка со встроенным
интерпретатором, а не компиляция в машинный код: исходник можно извлечь. `.env`,
переменные окружения и файлы ресурсов не встраиваются. Linux создаёт Linux-приложения,
Windows — `.exe`. Linux-пакет релизов собран статически на musl и не зависит от
версии glibc. Подробности: [docs/STANDALONE.md](docs/STANDALONE.md).

## Пример: сайт

```cpp
using server;
using json;

void home() {
    respond_html(template_render("<h1>Привет, {{name}}!</h1>", json_set("", "name", query_param("name"))));
}

void user() {
    respond(json_set("", "id", param("id")));
}

static_files("/static", "public");
get("/", "home");
get("/users/:id", "user");
listen(8080);
```

HTTPS включается вызовом `listen_tls(port, certificate, private_key)` без reverse
proxy. Больше примеров — в [examples/](examples): сайт-гостевая книга из нескольких
файлов с шаблонами, формами и загрузкой файлов ([examples/website](examples/website)),
файловый браузер с диалогами ([examples/file_browser.skm](examples/file_browser.skm)),
лабиринт в псевдо-3D на лучах, как в Wolfenstein 3D ([examples/raycaster.skm](examples/raycaster.skm)),
игра, частотный словарь, список дел в файле, HTTP-клиент, REST API, Telegram-боты на
webhook и long polling, TCP, терминал и графика.

## Платформы

| Платформа | Интерпретатор | Standalone | Проверка |
|---|---|---|---|
| Linux x86_64 | да | да | CI: Ubuntu и статическая сборка в Alpine |
| Windows x86_64 | да | да | CI: MSVC, запуск `.exe` в чистом каталоге |
| Android 9+ | NDK/JNI, AAR для Java/Kotlin | APK из `.skm` | CI: 4 ABI, эмуляторы API 28/35 |

Android: [SDK, сборка APK и ограничения платформы](docs/ANDROID.md).

```bash
python3 packaging/build_android.py examples/raycaster.skm --name "Лабиринт" -o game.apk
```

## Редакторы

`skrumi-lsp` (LSP 3.17) даёт диагностику при вводе, автодополнение, подсказки при
наведении и по параметрам, переход к определению и структуру файла. Описания
встроенных функций и модулей берутся из того же каталога, по которому работает
рантайм, поэтому подсказки всегда совпадают с языком.

В графическом установщике доступен выбор обнаруженных редакторов:
VS Code и Kate настраиваются автоматически. Zed устанавливается вручную. [Подробности и ограничения](docs/GUI_INSTALLER.md#выбор-редакторов).

* **VS Code** (подсветка, LSP, кнопка ▶ запуска, отладка по F5, проверка и сборка): установите `skrumi.vsix` из [последнего выпуска](https://github.com/SkrinVex/Skrumi/releases/latest/download/skrumi.vsix)
  командой `code --install-extension skrumi.vsix`.
* **Kate**: подсветка [`editors/kate/skrumi.xml`](editors/kate/skrumi.xml), настройки
  LSP [`editors/kate/settings.json`](editors/kate/settings.json) и внешние инструменты
  запуска, проверки и сборки [`editors/kate/externaltools/`](editors/kate/externaltools/),
  отладчик [`editors/kate/dap.json`](editors/kate/dap.json).
* **Zed**: dev-расширение из [`editors/zed/`](editors/zed/) с задачами запуска и отладчиком.

Пошаговая настройка: [docs/EDITORS.md](docs/EDITORS.md), отладка:
[docs/DEBUGGING.md](docs/DEBUGGING.md).

## Docker

```bash
docker run --rm ghcr.io/skrinvex/skrumi:latest --version
docker run --rm -v "$(pwd)":/app ghcr.io/skrinvex/skrumi:latest script.skm
```

Образ на Alpine с `skrumi`, `skrumi-lsp` и стандартной библиотекой публикуется
при каждом push в `master` и при каждом выпуске.

## Встраивание в C++

```c++
#include <skrumi/Skrumi.h>
#include <iostream>

int main() {
    skrumi::Interpreter interpreter;
    skrumi::RunResult result = interpreter.runSource("int answer = 40 + 2;");
    if (result.success) std::cout << interpreter.getGlobal("answer").text() << std::endl;
    return result.exitCode;
}
```

API описан в [разделе 22 документации](DOCUMENTATION.md#22-встраивание-в-c).

## Разработка

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

Тесты охватывают лексер, парсер, интерпретатор, анализатор и LSP (C++), регрессии
языка на Skrumi, ошибки и коды возврата, standalone-приложения, HTTP(S), TCP,
графику, а также проверяют, что каждый пример и каждый блок кода в документации
проходит `skrumi check`. Подробно: [tests/README.md](tests/README.md).

Версия хранится только в файле `VERSION`: CMake передаёт её в исполняемые файлы и
сам проставляет в манифесты расширений редакторов. Изменение `VERSION` в `master`
запускает сборку и публикацию выпуска.

### Бенчмарки

Перед каждым выпуском новая сборка соревнуется с предыдущим выпуском на одной машине:
если хоть один бенчмарк стал медленнее больше чем на 10%, выпуска нет. Те же задачи
написаны на Python, Lua, Ruby и JavaScript для сравнения; результаты прикладываются к
выпуску файлом `benchmarks.json` и показываются на [сайте](https://skrinvex.github.io/Skrumi/#speed).

```bash
python3 benchmarks/run.py                            # Skrumi и другие языки
python3 benchmarks/run.py --baseline путь/к/skrumi  # сравнение с другой версией
```

Подробно: [benchmarks/README.md](benchmarks/README.md).

```text
Skrumi/
├── VERSION              # единственное место с номером версии
├── include/skrumi/     # публичный C++ API: Interpreter, Builtins, Parser, SemanticAnalyzer
├── src/core/            # ядро skrumi_core: лексер, парсер, AST, рантайм, сеть
│   └── builtins/        # каталог встроенных функций, по разделам
├── src/graphics/        # программный рендерер и нативные окна
├── src/cli/             # команда skrumi
├── src/standalone/      # упаковка приложений
├── src/lsp/             # языковой сервер skrumi-lsp
├── std/                 # стандартная библиотека на Skrumi
├── examples/            # примеры программ
├── editors/             # VS Code, Kate, Zed
├── tests/               # unit, регрессионные, standalone и LSP-тесты
├── benchmarks/          # бенчмарки на Skrumi и других языках, run.py
└── docs/                # руководства: редакторы, графика, standalone
```

## Лицензия

MIT. Автор: [SkrinVex](https://skrinvex.su).

## History

Skrumi ранее назывался FoxLang. Начиная с major-релиза 9 обратная совместимость
удалена: единственные команда и расширение — `skrumi` и `.skm`.
[Ручной перенос программ и настроек](docs/MIGRATION.md). История проекта сохранена.
