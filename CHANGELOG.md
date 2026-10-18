# Skrumi 10.0.1 — optional editor integration in GUI setup

- Detect native VS Code and Kate installations on Windows/Linux and offer
  independent opt-in choices, rescan and restoration of previous selections.
- Install VS Code extension files offline in the user extension directory, with
  a private runtime/LSP path hint for custom installation directories.
- Install Kate syntax/external tools and merge the Skrumi LSP/debug profiles into
  existing JSON without replacing other profiles. Plugins remain user-controlled.
- Leave Zed installation manual; its dev-extension sources are not bundled in setup.
- Extend transaction journals to v4 for editor snapshots: install, update,
  deselection, cancellation, process interruption, recovery and uninstall.
  Preserve edited/unmanaged files and unrelated Kate configuration changes;
  continue reading older journals/manifests. No installer visual redesign.
- Add standalone regression coverage for integration ownership and recovery,
  and update component versions consistently. No language or VM changes.

# Skrumi 10.0.0 — explicit narrowing and deterministic calls

- Breaking: implicit float-to-int narrowing is rejected for declarations,
  assignments, parameters, results, fields and typed-container writes. Use
  to_int() explicitly, including for integral-valued floats. Builtin int
  parameters and array indices also require int. Int-to-float widening remains.
- CLI check and LSP report known narrowing in literals, numeric expressions,
  declared variables, calls, fields and nested container literals. Unknown
  dynamic types remain runtime-checked; this is not full static type inference.
- Breaking: remove the sized array declaration syntax. Use an ordinary empty
  declaration followed by resize(name, length). Remove its AST size member,
  compiler branch, bytecode instruction and VM handler.
- Breaking: a declared Skrumi function shadows a builtin by name, regardless of
  argument types/count. Direct calls and function values agree; invalid arguments
  do not select another implementation. Module aliases explicitly select exports.
- Correct the reference's array equality description and complete type_of
  results in the reference and builtin/LSP help. Execute marked documentation
  examples with assertions instead of only passing them through check.
- Add transactional Fish PATH integration in its native conf.d fragment, honoring
  XDG_CONFIG_HOME. Preserve user content, avoid duplicate PATH entries, safely
  quote literal paths and restore snapshots on rollback. csh/tcsh remain unsupported.
- Fix permission snapshots for a newly created shell configuration: use the same
  symbolic permission format as fs_permissions so a valid rollback is accepted.
- Migrate standard tests and installer arithmetic to explicit conversions; keep
  installer layout, GUI workflow, VM hot-path optimization and release assets.

# Skrumi 9.0.1 — recover installation after manual directory deletion

- Windows setup automatically recovers registration ownership when the main
  installation directory was deleted but the external maintenance executable
  and its ownership record remain intact.
- Recovery verifies the maintenance executable checksum and all seven expected
  registry values, including version, installation path and uninstall commands.
  Missing or modified registration values and unverified files remain protected.
- Recovery uses the existing transaction journal: cancellation or interruption
  restores the previous registry snapshots; successful uninstall removes the
  recovered registration instead of restoring a stale entry.
- Added regression coverage for manual deletion, older-version registration,
  rollback, repeated installation, uninstall and conflicting ownership evidence.
- Installer layout and language syntax are unchanged.

# Skrumi 9.0.0 — breaking removal of historical compatibility

- Source files now use `.skm` exclusively. Runtime file execution, CLI source
  tools, imports, standalone collection and Android staging reject retired formats.
- Unresolved imports now produce errors in CLI checks and LSP diagnostics,
  so an unavailable dependency cannot pass a source check as a warning.
- Removed caller-extension-dependent import selection, extension fallback and
  standard-library alias canonicalization. Standard precedence, relative paths,
  canonical module identity and cycle handling remain shared by runtime and tools.
- Audited all 90 `.fox` files before migration: 55 unique implementations moved
  to `.skm`; 30 equivalent examples removed after normalized comparison; five
  standard-library forwarding adapters removed. No unique implementation discarded.
- Promoted real native headers and Android implementations into Skrumi namespaces;
  removed forwarding headers, Java facades, duplicate native archives, executable
  copies, CMake compatibility options and environment fallbacks.
- Canonical native targets, exports, standalone descriptors, Android JNI/Gradle
  identities, browser factory/protocols and editor language IDs use Skrumi.
  Existing standalone executables remain independent; rebuild source projects
  and native/Android integrations for the new SDK.
- CLI is `skrumi`, language server is `skrumi-lsp`; only `SKRUMI_*` configuration
  and `--skrumi-*` launch options are recognized. Language syntax is unchanged.
- GUI layout and interaction remain unchanged. Payload now contains only the
  runtime, optional LSP, `.skm` stdlib and necessary maintenance/branding resources.
  Ownership verification, transaction journals, recovery and user-file protection
  remain in place; unknown installations are not searched for or removed.
- Release inventory contains two GUI installers, Android AAR, two demonstration
  APKs, VSIX, Kate syntax, benchmark JSON and SHA256SUMS. Internal archives,
  screenshots and profiling results stay CI artifacts, not release downloads.
- Updated VS Code, Zed and Kate identities and associations. Zed remains a source
  extension using its existing pinned C grammar; no unbuilt binary is published.
  The new VS Code identity requires an explicit installation, not a presumed
  Marketplace rename or automatic migration.
- Migrated examples, regressions, benchmarks, documentation, website, Playground
  and UI Builder. Added manual migration guidance and preserved historical records.
- Added negative compatibility tests, import/cycle/isolated-bundle regressions,
  package ownership inventory checks, checksum verification and a repository brand
  audit. Updated component versions together from 8.18.0 to 9.0.0.

Validation and platform limitations are recorded in `docs/REBRANDING_AUDIT.md`.

---

## Skrumi 8.18.0 — GUI installer and public system/UI APIs

- Highlight the selected directory preset, enable recovery only for a detected
  transaction, clarify platform-specific PATH activation, and verify preservation
  of preexisting unrelated files (including files inside `std`) and collision refusal.

- Compact directory presets into inline rows; separate removal from update options
  with a dedicated page and explicit confirmation before any filesystem changes.

- Start GUI setup and maintenance with a Russian/English language selector;
  localize controls, dialogs, operation logs and installer-owned diagnostics.

- Add installation directory presets and per-user discovery/locking to prevent
  duplicate managed installations; reuse the existing directory for updates.

- Make updated stdlib implementations canonical `.skm` sources with checked `.fox`
  compatibility adapters and one module identity. Prefer `.skm` in modern importers;
  preserve `.fox` precedence inside legacy importing files.
- Add explicit standalone resource packaging (`--resources`) and public resource
  reads without cwd fallback; retain v1 bundle support and add v2 resource records.
- Add native streaming SHA-256 through the existing Mbed TLS dependency.

- Add checked file reads, durable atomic writes, unique temporary directories and
  validated portable child paths to the public `fs` library. Browser durable writes
  explicitly fail; existing filesystem return conventions remain unchanged.
- Add opt-in deferred window close requests, Skrumi ProgressBar and nonblocking
  dialogs, and light/dark UI palettes shared by existing components.
- Add ProgressBar and application palettes to UI Builder, preserve format-1 projects,
  and add a Playground example and native/browser runtime tests.
- Reject NUL in process arguments, enforce the documented stdout capture limit,
  and report waitpid failures instead of an apparent successful exit.
- Add the offline `.skm` installer application and its journaled file transactions,
  ownership-aware update/uninstall, rollback, recovery, real progress and CLI checks.
- Add public process capture/cancellation, user-directory and persistent PATH plans,
  file locks, USTAR extraction, image decoding from memory, and Ctrl+A/X field editing.
- Add desktop work-area metrics, scalable fields/checkboxes and resizable installer
  layouts. Preserve old Builder sizing unless custom dimensions are selected.
- Add public HKCU string-value snapshot/compare-exchange APIs and journal Windows
  uninstall registration in Skrumi. Include a standalone maintenance application.
- Preserve user changes and import legacy Linux/Windows ownership manifests; support
  optional LSP installation and durable commit/rollback recovery records.
- Build the release from verified Windows and portable Linux GUI candidates, retain
  Android SDK/APKs and editor assets, and stop publishing language ZIP/TAR packages.
  A manual candidate build validates the release without creating tags or publishing.
- Resolve website downloads against actual latest-release assets, with no dead GUI
  links before the first release. See `docs/GUI_INSTALLER.md`
  for platform validation requirements and transaction guarantees.

- Rename the public language, CLI and LSP to Skrumi, `skrumi` and `skrumi-lsp`.
- Support `.skm` alongside `.fox` through the same runtime and tooling.
- Keep legacy CLI names, runtime flags/environment, C++/JNI/Java/editor identifiers,
  module precedence, repository URLs and project history.
- Update packaging, editor integrations, browser defaults and migration guidance.
- Add cross-name, mixed-extension and migration regression coverage.

# Изменения FoxLang

## FoxLang 8.17.2

- Windows: скрипты установщика сохранены в UTF-8 с BOM, чтобы `install.cmd` работал во встроенном Windows PowerShell 5.1 при русской и западной кодовых страницах.
- Установка переносит оба файла удаления; `uninstall.cmd` корректно завершается после удаления собственной папки. Устранён конфликт имени переменной с системной `$HOME`.
- Распаковка расширения VS Code без `code` CLI работает с файлами `.vsix` и в Windows PowerShell 5.1.
- CI проверяет ZIP, CMD-запуск, установку, обновление, VSIX и удаление в каталогах с кириллицей и пробелами; дополнительно проверяются прямые вызовы скриптов в PowerShell 7.

## FoxLang 8.17.1

- Android: исправлена отмена запуска, который ждёт общий рабочий поток, при уходе Activity в фон.
- Клавиатура runner открывается по запросу пользователя и не перекрывает игру при старте Activity.
- В релизах появился отдельный APK raycaster из `examples/raycaster.fox`; на Android игра открывается на полный экран с экранными кнопками.
- Системная трассировка и `packaging/profile_android.py` измеряют копирование кадров через JNI, запись Bitmap и вызовы рисования отдельно от VM.
- Android-тесты проверяют очередь двух интерпретаторов, отмену отложенных запусков, поворот, повторный запуск и пересоздание Activity. CI сохраняет трассы, сведения об эмуляторе и снимки памяти.

## FoxLang 8.17.0

- Android 9+: NDK/JNI и AAR для Java/Kotlin с отдельными экземплярами интерпретатора, асинхронным API, аргументами, вводом/выводом и отменой VM.
- FoxView выполняет существующие `graphics`/`ui`: кадры, касания, Unicode IME, аппаратные клавиши, изменение размера, fullscreen, буфер обмена и WAV/тоны. FoxControls даёт экранные кнопки с удержанием.
- Упаковка проектов `.fox` в APK с сохранением модулей и явно выбранных ресурсов, пример Activity и Gradle wrapper.
- Android CI: четыре ABI, выравнивание для страниц 16 КБ, instrumentation на API 28/35, AAR и демонстрационный APK в релизах.
- Документация, сайт и сведения о платформах синхронизированы; особенности файлов, потоков, разрешений и подписания APK описаны в `docs/ANDROID.md`.

## FoxLang 8.16.1

### Конструктор интерфейсов

- Разделены генерируемый `layout.fox` и редактируемый `main.fox`: обработчики сохраняются при правках макета, в JSON, предпросмотре и экспорте; ID удалённых элементов не переиспользуются.

- Новый сайт `site/builder/`: элементы UI, слои, перетаскивание, размеры и свойства, отмена/повтор, шаблоны, импорт и экспорт JSON-проекта.
- Отдельные макеты ПК и телефона с порогом ширины и привязками к краям. Генерация обычного `.fox` с состояниями полей и обработчиками кнопок.
- Живой запуск через WebAssembly-песочницу, не затрагивающий её сохранённый проект; экспорт кода и переход в песочницу.
- Обновлены объясняющий сайт, документация, ссылки и тесты генератора, браузера и настоящего runtime.

## FoxLang 8.16.0

### Запуск и нативные окна

- `build --no-console` создаёт Windows GUI EXE без появления консоли. `--console` выбирает консольную сборку; режим переопределяется при запуске через `--foxlang-console` и `--foxlang-no-console`.
- `--foxlang-width`, `--foxlang-height`, `--foxlang-fullscreen` и `--foxlang-windowed` управляют начальным окном исходной и собранной программы. `--` отделяет буквальные пользовательские аргументы для `os_args()`; `--foxlang-help` показывает справку runtime.
- Нативные окна по умолчанию изменяют размер мышью. При изменении сохраняется пересекающаяся часть холста; примеры Paint, игры и псевдо-3D учитывают новые размеры.
- `set_window_fullscreen` и `window_fullscreen` переключают полноэкранный режим Win32/X11 с восстановлением обычного окна. Добавлены проверки реальных окон, консоли Windows и разделения аргументов.

## FoxLang 8.15.2

### Веб-песочница и сборка

- Длинные строки кода полностью отображаются при горизонтальной прокрутке на ПК и телефоне.
- В мобильной панели управления, включая кнопку клавиатуры, отключены выделение текста и контекстное меню при удержании.
- Исправлено ожидание кадров в тесте интерфейса: чтение файла во время его перезаписи больше не пропускает ожидание и не срывает Docker-сборку.

## FoxLang 8.15.1

### Установка в Windows

- В Windows ZIP добавлены установщик и удаление без прав администратора.
- Установщик переносит интерпретатор, сервер LSP, стандартную библиотеку и DLL в постоянный каталог пользователя, добавляет FoxLang в PATH и устанавливает расширение VS Code. Повторный запуск обновляет установку.

## FoxLang 8.15.0

### Скорость

Теперь FoxLang быстрее Python 3.11 на всех бенчмарках. Задачи в 10 раз больше
стандартных (`benchmarks/`, медиана семи запусков; больше единицы — FoxLang быстрее):

| Бенчмарк | 8.14 | 8.15 |
|---|---|---|
| `loop` | 3.8 | 4.4 |
| `map_lookup` | 2.1 | 2.2 |
| `records` | 1.7 | 2.0 |
| `sort_numbers` | 1.8 | 1.9 |
| `array_sum` | 1.0 | 1.6 |
| `json_roundtrip` | 1.3 | 1.4 |
| `fib` | 1.15 | 1.4 |
| `split_join` | 1.0 | 1.35 |

- Массив растёт через `realloc`, как список в Python: большой блок памяти
  расширяется на месте, а не копируется поэлементно. `push` в цикле стал почти вдвое
  быстрее.
- Сложение и вычитание с числом прямо в коде (`n - 1`, `i + 2`) и сравнение с ним
  (`n < 2`) больше не занимают регистр под константу: у рекурсивных функций меньше
  работы на каждом вызове. Сам вход в функцию тоже стал короче.
- `join` сразу выделяет строку нужного размера, `split` по одному символу ищет его
  через `memchr`, `str_length` считает символы по 8 байт за раз.
- JSON: целые числа читаются без временной строки, у словарей сразу есть место под
  несколько ключей, ключ переносится в словарь без копии.
- Проверка типа структуры (`Point p = points[i]`) сравнивает имя сразу, без разбора
  типа.
- Когда программа закончилась, `foxlang` не освобождает её значения по одному:
  память возвращается системе целиком, вывод перед этим сбрасывается.

## FoxLang 8.14.0

### Скорость

Сравнение с Python 3.11 на задачах в 10 раз больше стандартных (`benchmarks/`,
медиана пяти запусков; больше единицы — FoxLang быстрее):

| Бенчмарк | Python / FoxLang |
|---|---|
| `loop` | 4.1 |
| `map_lookup` | 2.1 |
| `records` | 1.9 |
| `sort_numbers` | 1.7 |
| `json_roundtrip` | 1.3 |
| `fib` | 1.2 |
| `split_join` | 1.0 |
| `array_sum` | 0.9 (на стандартном размере 1.4) |

- Вызов функции FoxLang больше не проходит через рекурсию C++: виртуальная машина
  сама ведёт стек вызовов в одном цикле. Вызов стал дешевле, а рекурсия теперь
  доходит до 100 000 уровней вместо ~5800. Лямбды, методы и функции, которые
  вызывает встроенная, по-прежнему идут через стек потока.
- Аргументы-переменные передаются встроенным функциям без копирования, а у самых
  частых (`push`, `size`, `pop`, `str_length`, и обёрток вроде `length`) есть
  быстрый путь без общих проверок.
- Деление и остаток целых чисел считаются 32-битной командой процессора, копирование
  чисел между регистрами встроено в цикл VM.

## FoxLang 8.13.0

### Песочница в браузере

- Программа может состоять из нескольких файлов: вкладки над редактором,
  запускается `main.fox`, остальные подключаются через `using` и `include`, в том
  числе из папок. Ошибки отмечаются на вкладке своего файла, ссылка «Поделиться»
  сохраняет все файлы. Пример «Несколько файлов».
- Графика работает в браузере: окно `using graphics` рисуется на странице,
  клавиатура, мышь, колесо и касания доходят до программы (касание работает как
  щелчок, ведение пальцем как мышь с зажатой кнопкой). На телефоне под окном есть
  экранный джойстик: стрелки (можно держать две сразу, для диагонали) и «Пробел»,
  а кнопка «Клавиатура» открывает экранную клавиатуру, которая печатает в
  программу, включая Backspace и Enter. Игра работает сколько
  угодно, пока обновляет окно; зависшую страница останавливает через 10 секунд.
  Кадры идут с частотой экрана. Работает и модуль `ui`: кнопки, поля ввода,
  флажки, диалоги. Примеры «Игра в окне», «Окно с кнопками (ui)» и «Рисовалка».
  Для таких программ страница
  загружает сборку с Asyncify, она примерно вдвое медленнее обычной.
- `wait()` в песочнице с окном не замораживает страницу.
- Светлая и тёмная тема с переключателем (по умолчанию как в системе) и отдельная
  раскладка для телефона: панель в две строки, переключатель «Код / Вывод», после
  запуска сразу открывается вывод.
- Тексты страницы без длинных тире.

## FoxLang 8.12.0

### Скорость

| Бенчмарк | 8.10 | 8.12 |
|---|---|---|
| `map_lookup` | 0.144 с | 0.071 с |
| `loop` | 0.045 с | 0.036 с |
| `split_join` | 0.059 с | 0.054 с |

- Большой словарь (больше восьми ключей) ищет ключ через компактную хеш-таблицу
  позиций вместо второй копии каждого ключа: поиск и вставка вдвое быстрее, памяти
  нужно меньше. Хеш со случайным зерном на каждый запуск, так что заранее
  подобранные ключи не замедлят сервер.
- Память под строки, как и под контейнеры, берётся из списка свободных ячеек.
- Арифметика и сравнения над значениями, про которые компилятор знает, что это
  `int` (типизированные переменные, целые литералы), выполняются без проверки
  типов — только с проверкой переполнения.

### Исправления

- Присваивание переменной `int` числа вне диапазона (`x = 3000000000;`) теперь
  даёт ошибку, как и объявление с таким числом, а не сохраняет его.
## FoxLang 8.11.0

### Песочница в браузере

- На сайте появилась [песочница](https://skrinvex.github.io/FoxLang/playground/):
  FoxLang можно попробовать без установки. Интерпретатор собран в WebAssembly и
  работает в браузере посетителя, на сервер ничего не отправляется.
- Редактор с подсветкой, номерами строк и автоотступами; ошибки подчёркиваются во
  время набора. Шесть примеров, поле для ввода программы (`input()`), ссылка
  «Поделиться» с самой программой внутри; черновик сохраняется в браузере.
- Ограничения: 10 секунд на запуск (кнопка «Стоп» — в любой момент), миллион
  символов вывода, 512 МБ памяти, 1000 уровней вызовов. Сети, окон, звука и
  запуска процессов в браузере нет — такие функции сообщают об этом понятной ошибкой.
- Сборка для WebAssembly: `emcmake cmake -S . -B web`; её проверяет CI.

## FoxLang 8.10.1

### Командная строка

- `foxlang help` (а также `foxlang --help` и `foxlang` без аргументов) показывает
  все команды по группам — запуск и проверка, сборка, инструменты — с примерами и
  переменными окружения.
- `foxlang help <команда>` и `foxlang <команда> --help` рассказывают об одной
  команде подробно: например, `foxlang help build` — какие параметры есть у сборки,
  как называется готовый файл, что в него попадает и что нет.
- `--help` после имени скрипта по-прежнему достаётся самой программе.

## FoxLang 8.10.0

### Скорость

Время бенчмарков из `benchmarks/` по сравнению с 8.9.1 (Linux x86_64, медиана):

| Бенчмарк | 8.9.1 | 8.10.0 |
|---|---|---|
| `json_roundtrip` | 0.125 с | 0.054 с |
| `loop` | 0.082 с | 0.045 с |
| `records` | 0.116 с | 0.084 с |
| `array_sum` | 0.044 с | 0.036 с |
| `sort_numbers` | 0.077 с | 0.061 с |
| `fib` | 0.031 с | 0.026 с |
| `split_join` | 0.074 с | 0.058 с |
| `map_lookup` | 0.163 с | 0.149 с |

- Переменные верхнего уровня программы живут в регистрах виртуальной машины, как
  локальные переменные функций: циклы в основном коде скрипта больше не ищут
  переменные по имени.
- Команды виртуальной машины выбираются переходом по таблице (computed goto) там,
  где его поддерживает компилятор; если тип выражения известен при компиляции,
  лишние проверки и преобразования не выполняются.
- `json_decode` разбирает текст за один проход, `json_value` собирает ответ в один
  буфер — оба стали примерно вдвое быстрее и быстрее Python.
- Запуск программы: 1.8 мс вместо 4.8 мс — исполняемый файл больше не читается
  целиком, чтобы проверить, нет ли в нём встроенной программы.
- Память под массивы, словари и структуры берётся из списка свободных ячеек.
- Сборка Release использует оптимизацию при компоновке (LTO), выключается
  `-DFOXLANG_LTO=OFF`. Глубина рекурсии выросла с ~4500 до ~5800 вызовов.
- Функция, которая только передаёт свои параметры встроенной (так устроена
  стандартная библиотека: `draw_rect` вызывает `gfx_rect`), вызывает её без
  собственного кадра. Ошибка внутри такой функции теперь указывает на строку
  программы, а не на строку в `std/graphics.fox`.

### Графика

- Вызов графической функции больше не ищет её по имени среди всех остальных: кадр
  с 2000 прямоугольниками готов за 0.7 мс вместо 2.2–3 мс, 20 000 спрайтов через
  `draw_image_part` — за 8.6 мс вместо 27 мс.
- На Linux кадр передаётся X-серверу через разделяемую память (MIT-SHM): показ
  кадра 1280×720 занимает 1.2 мс вместо 3 мс. На удалённом дисплее или без
  System V shared memory используется прежний способ; `FOXLANG_X11_SHM=0`
  выключает MIT-SHM принудительно.
- Новые функции для игр: `draw_tiles` рисует карту плиток из атласа одним вызовом
  и пропускает плитки за краем окна, `draw_sprites` — много кадров атласа,
  `draw_rects` — много прямоугольников.
- Картинки рисуются без деления на каждый пиксель.

### Изменения поведения

- `json_decode` строже: текст вроде `tru` раньше превращался в `false`, теперь это
  ошибка, как и любой другой некорректный JSON.

## FoxLang 8.9.1

### Память

- Сборщик циклов. Кольца контейнеров, которые ссылаются только друг на друга
  (массив внутри себя, две структуры с полями друг на друга, лямбда в словаре,
  который она видит), раньше жили до конца программы. Теперь сборщик находит их
  сам, когда с прошлого раза создано достаточно контейнеров (не меньше 100 000 и не
  меньше восьми живых наборов), так что долго работающий сервер не копит память.
  `gc_collect()` запускает его сразу и возвращает число освобождённых контейнеров.
  Под AddressSanitizer утечек больше нет.
- Цена: на программах, которые держат сотни тысяч контейнеров, — около 5–10%
  времени на бенчмарках `records` и `json_roundtrip`; остальные не изменились.

### Язык

- Встроенные функции вызываются и через псевдоним модуля: `m.sqrt(2)`. Сам
  псевдоним — константа, его содержимое изменить нельзя.
- Значения перечисления и псевдонима модуля защищены везде, включая `remove` и
  изменение значений в отладчике; константа и типы `T?`, `array<T>` соблюдаются и
  при правке переменных в отладчике.

### Отладчик

- Локальная переменная со значением `null` видна в панели переменных (раньше она
  пряталась вместе с ещё не объявленными). `null` и значения перечислений
  показываются по имени.

### Сайт

- Версия и цифры в шапке сайта (встроенные функции, модули, тесты) больше не
  устаревают: их сверяет тест `site_facts`, обновляет
  `python3 packaging/site_facts.py --build build --write`.

## FoxLang 8.9.0

### Язык

- Типизированные контейнеры: `array<int>`, `map<string, float>`,
  `array<array<string>>`, `array<Point>?`. Элементы приводятся к типу при
  объявлении и при каждой записи (`items[i] = ...`, `m[key] = ...`, `push`,
  `insert`, `set`); неподходящее значение — `Type Error`. Тип получает сам массив,
  а массив, типизированный по-другому, не подменяется незаметно. `resize` добавляет
  пустые значения типа элемента.
- `array?` теперь действительно может хранить `null` (раньше тип массива при
  объявлении терялся).

### Инструменты

- `foxlang check` проверяет тип элемента и ключ `map<string, T>`; отладчик
  показывает тип `array<int> (3)`.
- Ошибки разбора в новых конструкциях (`enum`, `const`, методы, типы контейнеров)
  больше не дописывают бессмысленное «but found …».

## FoxLang 8.8.0

### Язык

- Константы: `const int MAX = 10;`. Присвоить константе новое значение (`=`, `+=`,
  `++`) нельзя — ни на верхнем уровне, ни из функции, ни из лямбды; содержимое
  массива, словаря или структуры менять можно.
- Перечисления: `enum Color { Red, Green, Blue }`, `enum Status { Active = "active" }`.
  Значение `Color.Red` имеет тип `Color`, `.name` и `.value`; печатается по имени,
  сравнивается `==`, работает в `switch`, в JSON — имя строкой. Имя перечисления —
  неизменяемый словарь его значений: `for (name, c in Color)`, `size(Color)`.
  Поле или переменная типа перечисления без значения получает первое значение.

### Инструменты

- `foxlang check` и редактор сообщают о присваивании константе и о несуществующем
  значении перечисления; после `Color.` автодополнение предлагает значения.
  В списке символов файла структуры, перечисления и методы получили свои значки.
- Подсветка `const` и `enum` в VS Code, Kate и Zed.

## FoxLang 8.7.0

### Язык

- `null` и типы `T?`: `string? nick = null;`. Переменные, параметры, поля структур и
  результаты функций типа `T?` хранят значение `T` или `null`; обычные типы `null`
  не принимают. `T?` без начального значения равен `null`, функция `T?` без
  `return` возвращает `null`.
- `a ?? b` — `a`, если оно не `null`, иначе `b` (правая часть вычисляется только
  тогда). `user?.boss?.name` и `maybe?.method()` дают `null` вместо ошибки.
- `x == null` / `x != null`; `print(null)`, интерполяция и склейка показывают
  `null`, `type_of(null)` — `"null"`. Результат `void`-функции теперь тоже
  показывается как `null` (раньше — пустая строка).
- Поле `func?` можно не задавать при создании структуры.

### Встроенные функции

- `json_decode` превращает JSON `null` в `null` (раньше — в пустую строку),
  `json_value(null)` даёт `null`. Встроенные функции с параметром `any` (`print`,
  `push`, `set`, …) принимают `null`.

### Инструменты

- `foxlang check` и редактор сообщают, когда `null` попадает в переменную, параметр
  или результат функции без `?`, и понимают `T?` в типах полей и методов.
- Подсветка `null`, `??` и `?.` в VS Code, Kate и Zed.

## FoxLang 8.6.0

### Язык

- Псевдоним модуля: `using math as m;` подключает модуль и создаёт `m`, через
  которую его функции и переменные доступны с точкой: `m.hypot(3, 4)`, `m.PI`.
  Вызов через псевдоним ведёт в модуль, даже если в программе появилась своя функция
  с тем же именем.

### Инструменты

- `foxlang check` и редактор проверяют имена и число аргументов после псевдонима
  модуля и подсказывают, что встроенную функцию (`m.sqrt`) вызывают без него; после
  `m.` автодополнение предлагает функции и переменные модуля.
- Подсветка `as` в VS Code, Kate и Zed.

## FoxLang 8.5.0

### Язык

- Методы структур: функция, объявленная внутри `struct`, вызывается через точку —
  `p.move(1, 2)`, а внутри метода `this` — значение, у которого он вызван
  (`this.x += dx;`). Методы меняют саму структуру, вызывают друг друга и доступны
  выше строки объявления структуры, как и функции.
- Функция в поле структуры или в ключе словаря вызывается через точку:
  `button.on_click()`, `handlers.save()`.
- Результат любого выражения можно вызвать сразу: `make_adder(1)(2)`,
  `steps[0](x)`.
- В ошибках и в стеке отладчика метод называется `Point.move`; неверное число
  аргументов сообщается без учёта `this`.

### Инструменты

- `foxlang check` и редактор знают поля и методы: сообщают о несуществующем поле или
  методе, о неверном числе аргументов метода и о вызове поля, в котором не функция.
  После `имя.` автодополнение предлагает поля и методы структуры.
- Отладчик ставит точки останова внутри методов, лямбд, циклов `for (x in ...)` и
  блоков `try`/`catch`/`finally`; раньше точка в теле `for-in` или `try` уезжала на
  следующую строку снаружи.
- Подсветка `this` в VS Code, Kate и Zed.

## FoxLang 8.4.0

### Язык

- Функцию и структуру верхнего уровня можно использовать выше строки, где они
  объявлены: `print(g()); int g() { return 1; }` больше не ошибка «not found». Если
  функция с таким именем уже есть (например, из модуля), до строки нового объявления
  действует прежняя.

- Интерполяция строк: `"Привет, ${name}! Тебе ${age + 1}"`. Значение подставляется
  так, как его показывает `print`; внутри `${ }` — любое выражение. `\$` — знак
  доллара без подстановки. Подсветка в VS Code и Kate, проверка имён в `foxlang check`
  и в редакторе.

- Цикл по элементам: `for (x in items)`, `for (i, x in items)`,
  `for (key, value in ages)`, `for (ch in text)` — по массиву, словарю и символам
  строки, с необязательным типом у переменных (`for (string k, int v in ages)`).
  `in` — не ключевое слово, переменные с таким именем продолжают работать.

- Функции как значения и лямбды. Тип `func` хранит функцию: лямбду
  `(int x) => x * 2`, `x => x + 1` или `(a, b) => { ... }`, функцию FoxLang или
  встроенную по имени (`func plus = add; func say = print;`). Переменную `func`
  вызывают как функцию, функции передают аргументами и возвращают из функций.
- Замыкания: лямбда видит локальные переменные функции, где создана, и после выхода
  из неё; переменная общая, а в цикле `for (x in items)` у каждой итерации своя.
  Локальная лямбда может вызывать себя по имени своей переменной. Переменные, которые
  лямбды не захватывают, остаются в регистрах, так что обычный код не замедлился.
- Параметр функции можно объявить без типа — он принимает значение любого типа:
  `void log_all(label, value)`.
- Вызов переменной, в которой не функция, — понятная ошибка
  `Type Error: 'x' is not a function, it holds a value of type 'int'`, а
  `foxlang check` и редактор сообщают о ней заранее.

### Встроенные функции и библиотека

- `array_map`, `array_filter`, `array_reduce`, `array_find`, `array_any`,
  `array_all` принимают функцию; `array_sort(items, before)` сортирует устойчиво по
  функции сравнения. В модуле `arrays` — обёртки `transform`, `filter`, `reduce`,
  `sort_by`, `find_first`, `any_of`, `all_of`.
- Обработчик маршрута сервера (`get`, `post`, `route`, `not_found`, `server_route`,
  `server_not_found`) может быть функцией или лямбдой, а не только именем строкой:
  `get("/ping", () => { respond("pong"); });`.

### Редакторы

- Подсветка `func` и `=>` в VS Code, Kate и Zed; подсказка по `func` в языковом
  сервере.

## FoxLang 8.3.0

### Байткод вместо обхода дерева

- Программа больше не выполняется обходом дерева. Компилятор переводит каждую функцию
  (при первом вызове) и верхний уровень программы в байткод регистровой виртуальной
  машины, как в Lua: переменные функции — её первые регистры, `return`, `break` и
  `continue` — переходы, `finally` копируется на каждый выход из `try`, а для ошибок у
  функции есть таблица обработчиков. Глобальные переменные и вызовы запоминают, что
  нашли, прямо в команде.
- `foxlang disasm program.fox` печатает байткод программы и её функций.
- Скорость на той же машине, 8.2.0 → 8.3.0 (с): `fib` 0,050 → 0,030, `loop` 0,170 →
  0,078, `array_sum` 0,089 → 0,049, `records` 0,143 → 0,094, `split_join` 0,100 →
  0,064, `sort_numbers` 0,095 → 0,070. На `loop`, `records` и `sort_numbers` FoxLang
  теперь быстрее Python 3.11, на `fib` и `split_join` — вровень.
- Рекурсия уходит глубже: около 4700 вложенных вызовов вместо 1700 на 8 МБ стека.
- Отладчик работает как раньше: при подключённом отладчике функции компилируются с
  командами «началась строка» и «блок открылся/закрылся».
- Прежний интерпретатор по дереву удалён. Переход проверялся так: 118 программ
  (все тесты, бенчмарки и 84 коротких случая на ошибки, строки, `try`/`finally`,
  `switch`) выполнялись обоими способами с одинаковым выводом, ошибками и кодами
  возврата. Короткие случаи остались тестом `programs` с записанным ожидаемым
  результатом (`tests/test_programs.json`).
- Консоль отладчика и значения полей структур по умолчанию тоже компилируются в
  байткод.

### Изменения поведения

- Ошибка сообщает строку, где она случилась. Раньше ошибка, прошедшая через `finally`,
  называла последнюю строку блока `finally`, а ошибка в условии цикла — последнюю
  выполненную строку тела цикла.
- Значения полей структуры по умолчанию вычисляются в глобальной области: они видят
  глобальные переменные, но не локальные переменные того, кто создаёт структуру.

### Встраивание в C++

- Встроенные функции получают аргументы через `foxlang::Arguments` (вид на
  последовательные значения) вместо `std::vector<Value>`; `runtime::invoke`,
  `acceptsArguments` и `callBuiltin` принимают его, вектор к нему приводится сам.

## FoxLang 8.2.0

### Скорость: новое представление значений

- Значение занимает 16 байт вместо примерно 100: `int`, `float` и `bool` хранятся
  прямо в нём, строка и контейнер — общим указателем со счётчиком ссылок. Копия
  значения больше не копирует текст, а арифметика, сравнения и условия проверяют вид
  значения, а не сравнивают строки `"int"` и `"true"`.
- Вызов функции ничего не выделяет в куче, пока у функции не больше восьми
  аргументов и локальных переменных, и не собирает текст сообщения об ошибке, если
  ошибки нет. Блок внутри функции или цикла не создаёт своей области имён: его
  переменные и так живут в пронумерованных ячейках.
- Встроенные функции проверяют типы аргументов по заранее вычисленному виду.
- Итог по бенчмаркам на той же машине, 8.1.0 → 8.2.0 (с): `fib` 0,25 → 0,05,
  `loop` 0,91 → 0,19, `array_sum` 0,33 → 0,08, `records` 0,41 → 0,15,
  `split_join` 0,27 → 0,09, `sort_numbers` 0,25 → 0,09, `map_lookup` 0,26 → 0,16,
  `json_roundtrip` 0,15 → 0,12.

### Встраивание в C++

- `foxlang::Value` устроен иначе: вместо полей `type`, `value` и `ref` — методы
  `kind()`, `asInt()`, `asFloat()`, `asBool()`, `str()`, `ref()`, `text()` и
  `typeName()`, новые значения создают `Value::integer`, `Value::real`,
  `Value::boolean` и `Value::string`. Классы `Text` и `TypeName` удалены.
  `getGlobal("x").value` теперь пишется `getGlobal("x").text()`.
- `setGlobal(имя, тип, текст)` проверяет текст: `setGlobal("n", "int", "abc")` — ошибка
  `Type Error`, а не переменная `int` с нечисловым текстом.

### Исправления

- `using имя;` ищет сначала стандартный модуль (на диске или встроенный), и только
  потом свой файл `имя.fox`. Раньше файл `arrays.fox` рядом с программой молча
  подменял `using arrays;`, и ошибка выглядела как не связанная с ним
  («Variable 'n' is already declared»). Свой модуль с именем стандартного
  подключается через `include`.
- Документация: функция может возвращать `map` и структуру, а не только
  `int`/`float`/`string`/`bool`/`array`/`void`.
- Целый литерал, который не помещается даже в 64 бита, — ошибка выполнения
  «int literal '…' does not fit in int» при любом использовании; раньше `print` мог
  напечатать его цифры.
- Тест `ui_interaction` запускается отдельно от остальных (`RUN_SERIAL`): при
  параллельном `ctest -j` он иногда падал.

## FoxLang 8.1.0

### Бенчмарки

- Папка `benchmarks/`: восемь задач (вызовы функций, цикл, массивы, словари, строки,
  структуры, сортировка, JSON), каждая на FoxLang, Python, Lua, Ruby и JavaScript.
  Программы печатают контрольное число, и у всех языков оно обязано совпасть.
- `benchmarks/run.py` запускает их, берёт лучший из нескольких запусков и сравнивает
  с другой сборкой FoxLang через `--baseline`.
- Выпуск не публикуется, если новая сборка медленнее предыдущего выпуска больше чем на
  10% хотя бы на одном бенчмарке. Результаты прикладываются к выпуску файлом
  `benchmarks.json`, сайт показывает их графиками.
- Тест `benchmarks_smoke` прогоняет все бенчмарки на маленьких входах.

### Скорость

- Словарь находит ключ по хеш-индексу, а не перебором: 200 000 ключей раньше
  обрабатывались больше четырёх минут, теперь меньше секунды.
- Вызов функции с верхнего уровня программы больше не спрашивает у системы размер
  стека каждый раз (на Linux это чтение файла); такие вызовы стали примерно в 60 раз
  быстрее.

## FoxLang 8.0.0

### Одно правило для контейнеров

- Массивы, словари и структуры передаются по ссылке везде, как в Python, JavaScript и
  Java: присваивание `b = a`, объявление, передача в функцию и возврат из неё дают ту же
  самую коробку. Раньше присваивание копировало, а передача в функцию — нет; такое
  смешанное правило не совпадало ни с одним массовым языком.
- `copy(value)` — независимая копия со всем вложенным.
- Контейнер, который содержит сам себя (`push(a, a)`), печатается как `[...]`, а `==` и
  `copy` с ним справляются.

### Скорость

- Локальные переменные функций и блоков получают номера ячеек заранее, до первого
  вызова: доступ к ним — индекс в массиве, а не поиск по имени в словарях.
- Тип значения хранится как вид и указатель на общее имя, а не как строка в каждом
  значении.
- Глобальные переменные и вызываемые функции ищутся по имени один раз, дальше узел
  дерева помнит, где они.
- Вместе с этим программы быстрее, чем в 7.0.0, примерно на четверть.

### Исправления

- Структура, объявленная в файле, подключённом через `include`, не регистрировалась.

### Несовместимые изменения

- `array b = a; b[0] = 1;` меняет и `a`. Чтобы получить независимую копию, пишите
  `array b = copy(a);`.
- Результат функции, вернувшей глобальный массив или параметр, — тот же массив, а не копия.

## FoxLang 7.0.0

### Язык

- Словари `map`: литералы `{"key": value, word: value}`, чтение и запись `m["key"]` и
  `m.key`, функции `keys`, `values`, `has`, `remove_key`, `get_or`, `size`.
- Структуры `struct` с типизированными полями и значениями по умолчанию: `Point(1, 2)`,
  `Point p;`, поля `p.x`, структуры как типы переменных, параметров и результатов.
- Вложенные контейнеры работают везде: `grid[1][0] = 5;`, `users[0].tags[1]`,
  `config.server.port`, в том числе результат вызова: `json_decode(text)["items"][0]`.
- `try` / `catch` / `finally` и `throw`: перехватываются все ошибки выполнения, `finally`
  выполняется и при `return`, `break`, `continue`, `exit`.
- `json_decode` превращает JSON в словари, массивы и числа; `json_value` записывает
  словари и структуры как JSON-объекты.
- Массивы, словари и структуры — объекты со счётчиком ссылок: временные значения
  освобождаются сами, присваивание и возврат из функции копируют содержимое целиком,
  в функцию контейнер передаётся для изменения.

### Скорость

- `return`, `break` и `continue` больше не используют исключения C++: вызов функции
  стал примерно в 8 раз быстрее, типичная программа — в 5 раз.
- Операторы определяются один раз при разборе, сложение и сравнение целых идут без
  преобразований, присваивание значения своего типа не собирает текст сообщения.

### Инструменты

- `foxlang test`: функции `test_...()` в файлах `*_test.fox`, каждая в свежей программе,
  фильтр `--filter`, встроенные `assert` и `assert_equal`.
- `foxlang fmt [--check]`: единое оформление — отступы, пустые строки, пробелы в конце
  строк, переводы строк; код не переписывается.
- Языковой сервер: поиск всех ссылок и переименование по всем файлам программы
  (включая неоткрытые), форматирование документа. VS Code: `Shift+F12`, `F2`,
  `Shift+Alt+F`.
- Отладчик: отдельные фильтры «Необработанные ошибки» и «Перехваченные ошибки»,
  словари и структуры в панели переменных, изменение полей и значений.

### Графика и звук

- Картинки PNG (все виды, прозрачность, палитры, Adam7) и BMP без внешних библиотек:
  `load_image`, `draw_image`, `draw_image_scaled`, `draw_image_alpha`,
  `draw_image_part`, `image_pixel`, `image_alpha`, `free_image`.
- Окно с изменяемым размером: `set_window_resizable`, `set_window_size`,
  `window_resized`; область рисования следует за окном.
- Модуль `sound`: `play_sound` (WAV), `play_tone`, `beep`, `stop_sounds`. Windows —
  `PlaySound`, Linux — проигрыватель рабочего стола (`pw-play`, `paplay`, `aplay`).

### Несовместимые изменения

- Новые ключевые слова `map`, `struct`, `try`, `catch`, `finally`, `throw` больше нельзя
  использовать как имена переменных и функций.
- `==` сравнивает массивы по содержимому, а не по тому, один ли это массив.
- Строка плюс массив даёт текст массива (`"x" + [1]` — `x[1]`) вместо служебного имени.
- Фильтр ошибок отладчика `errors` заменён на `uncaught` и `caught`.

## FoxLang 6.4.0

### Отладчик

- `foxlang debug-adapter` — отладчик по протоколу Debug Adapter Protocol, общий для
  VS Code, Kate и Zed:
  - точки останова, условные, по числу срабатываний и лог-точки; точка на пустой строке
    переносится на ближайший оператор;
  - шаги с заходом в функцию, через функцию и выход из неё, продолжение и пауза;
  - стек вызовов (в том числе функции встроенных модулей, их текст открывается по щелчку);
  - локальные и глобальные переменные, массивы по элементам, изменение значений;
  - выражения в наблюдении, при наведении и в консоли, где выполняются и операторы;
  - остановка на операторе, где произошла ошибка выполнения, пока переменные на месте.
- VS Code: F5, «FoxLang: отладить программу» в меню кнопки ▶, `launch.json` с полями
  `program`, `args`, `cwd`, `stopOnEntry`, `env`. Программа работает в своём терминале:
  `input()`, цвета и графика ведут себя как при обычном запуске.
- Kate: настройки плагина «Отладчик» в `editors/kate/dap.json`, `install.sh` ставит их сам.
- Zed: расширение регистрирует отладчик FoxLang (`debugger: start`, `.zed/debug.json`).
- Без отладчика интерпретатор работает как прежде: одна проверка на блок и на вызов.
- Руководство: `docs/DEBUGGING.md`.

## FoxLang 6.3.0

### Прокрутка

- `ui_scroll_begin`/`ui_scroll_end` — прокручиваемая область: колесо мыши над ней, полоса
  прокрутки у правого края, содержимое обрезается по области. Строки, уехавшие за край,
  не перехватывают щелчки у соседних элементов.
- `ui_scrollbar` — отдельная полоса прокрутки в любых единицах: ползунок тянется мышью и
  держится, даже если мышь ушла за полосу, щелчок по дорожке листает на страницу.
- `ui_scroll_to` — смещение, при котором выбранная строка видна, для навигации стрелками.
- Колесо получает самая внутренняя область под мышью (`ui_wheel`); под модальным окном
  списки не прокручиваются.
- `ui_drag`, `ui_drag_x`, `ui_drag_y` — элементы, которые тянут мышью: ползунки,
  разделители панелей. Пока элемент тянут, остальные не подсвечиваются.
- `clip_begin`/`clip_end` в `graphics` — рисование только внутри прямоугольника,
  вложенные области сужают друг друга.
- Цвета полосы — `UI_TRACK`, `UI_THUMB`, `UI_THUMB_ACTIVE`, шаг колеса — `UI_SCROLL_STEP`.

### Исправления

- Поле, получившее фокус через `ui_focus` в этом кадре, не принимает ввод этого же кадра:
  горячая клавиша-буква, открывшая диалог (например, `G` — «Перейти»), больше не
  попадает в его поле ввода.
- Подсказка `placeholder` в `ui_text_field` видна, пока поле пустое, в том числе в фокусе:
  в только что открытом диалоге сразу понятно, что вводить.

### Шрифт

- У строчных `д`, `ц`, `щ` хвосты опускаются ниже строки: `д` больше не похожа на `а`.

### Пример

- `examples/file_browser.fox` прокручивает список колесом и ползунком попиксельно и
  рисует только видимые строки.

## FoxLang 6.2.0

### Интерфейсы, которые ведут себя как в обычных программах

- Новый модуль `ui`: `ui_button`, `ui_primary_button`, `ui_text_field`, `ui_checkbox`,
  `ui_modal_begin`/`ui_modal_end`, слои, `ui_click`, `ui_hover`, `ui_double_click`,
  фокус (`ui_focus`, `ui_focused`, `ui_typing`) и тема через переменные `UI_*`.
- Щелчок получает только верхний элемент: кнопка, открывшая диалог, больше не нажимает
  в том же кадре кнопку диалога, а перекрытые элементы не реагируют. Модальное окно
  отключает всё под собой — кнопки, поля и вкладки за ним не подсвечиваются, не
  нажимаются и не получают ввод с клавиатуры.
- Фокус клавиатуры: щелчок по полю ставит курсор, Tab и Shift+Tab переходят между
  полями, щелчок мимо полей снимает фокус (в диалоге фокус остаётся), пока идёт ввод,
  `ui_typing()` позволяет не выполнять горячие клавиши-буквы.
- Поле ввода с курсором: набор в любой раскладке, Backspace и Delete с автоповтором,
  стрелки, Home и End, Ctrl+V и Ctrl+C, прокрутка длинного текста. Нажатия внутри одного
  кадра применяются по порядку: быстрый набор «x, Backspace, y» даёт «y».

### Ввод в графике

- `text_input()` — набранный текст с учётом раскладки (русской тоже), Shift, Caps Lock
  и автоповтора. Раньше текст можно было собрать только из `key_pressed("A".."Z")`, без
  кириллицы, `/`, `.` и `-`.
- `key_repeat(key)` — нажатие и системный автоповтор, `mouse_wheel()`, `double_clicked()`,
  `clipboard_text()`/`set_clipboard_text()` (X11 CLIPBOARD и буфер Windows),
  `draw_rect_alpha` для затемнения и полупрозрачных панелей, `window_width()`/`window_height()`.
- Клавиши `DELETE`, `INSERT`, `HOME`, `END`, `PAGE_UP`, `PAGE_DOWN`, `F1`–`F12`, `MOUSE_MIDDLE`.
- Шрифт рисует строчные буквы латиницы и кириллицы (раньше весь текст был прописным),
  у `g p q y j` — настоящий спуск.

### Файловая система и процессы

- Права доступа (`fs_permissions`, `fs_set_permissions`, `fs_is_executable`), владелец,
  время изменения, `fs_is_file`, `fs_is_link`, свободное место, домашний и временный
  каталоги.
- `fs_copy` и `fs_move` для файлов и каталогов целиком (перенос и между дисками) и
  `fs_remove_all` с защитой корня диска и домашнего каталога — раньше каталоги
  приходилось копировать и удалять рекурсией на FoxLang, читая файлы в строки.
- Функции путей: `path_join`, `path_parent`, `path_name`, `path_stem`,
  `path_extension`, `path_absolute`; в `using fs;` — короткие имена и `format_bytes`.
- `fs_size` возвращает `float`: файлы больше 2 ГиБ больше не переполняют `int`.
- `os_open` открывает файл или URL приложением по умолчанию, `os_run` и
  `os_run_output` запускают программы без командной оболочки.

### Примеры и тесты

- [examples/file_browser.fox](examples/file_browser.fox): файловый браузер с правами,
  размерами, датами, колесом, двойным щелчком и модальными диалогами.
- Тест `ui_interaction` управляет настоящим окном событиями ОС и проверяет модальность,
  фокус, ввод, автоповтор, колесо и двойной щелчок; на Windows — и ввод кириллицы.

## FoxLang 6.1.1

- VS Code: кнопка ▶ и команды проверки и сборки работают для файла, открытого без
  папки. Раньше VS Code отвечал «Variable workspaceFolder can not be resolved»:
  задача создавалась в области рабочей папки, а сопоставитель ошибок ссылался на
  `${workspaceFolder}`. Теперь пути в ошибках всегда полные, файл из открытой папки
  запускается как задача с разбором ошибок, а отдельный файл — в терминале.
- `install.sh` ставит расширение VS Code одним способом: через `code --install-extension`,
  если утилита есть, и копированием каталога только для редакторов без неё. Раньше
  получались две копии — два языковых сервера и дважды зарегистрированные команды.
  Прежняя скопированная копия удаляется при установке.
- Выпуск содержит один `foxlang.vsix` вместо двух одинаковых пакетов и без
  `foxlang-vscode.zip`, дублировавшего то же расширение архивом.

## FoxLang 6.1.0

### Веб-сервер

- Маршруты с параметрами: `get("/users/:id", ...)`, `get("/files/*path", ...)` и
  `param(name)`. Точные пути проверяются раньше шаблонов, GET отвечает и на HEAD,
  путь с другим методом получает `405` с заголовком `Allow`. Добавлены `patch`.
- Статические файлы: `static_files("/static", "public")` с типом содержимого по
  расширению, `index.html` для каталогов и перенаправлением на адрес с `/`. Скрытые
  файлы (`.env`) и выход из каталога через `..` (в том числе закодированный) не отдаются.
- Ответы файлами: `send_file(path)`, `download(path, filename)` с именем на кириллице,
  `respond_html`, `respond_text`, `render(template, data)`.
- Запрос: `query_param(name)` с декодированием, поля форм `form(name)` для
  urlencoded и `multipart/form-data`, загрузка файлов `upload_name` и `save_upload`,
  `cookie(name)`, `client_ip()`.
- Ответ: `redirect(url)`, `set_header`, `set_cookie` с атрибутами; значения
  заголовков с переводом строки отклоняются.
- `not_found(handler)` — своя страница 404, `allow_cors(origin)` — CORS и ответы на
  `OPTIONS`, `access_log(true)` — строка о каждом запросе, `max_body_size(bytes)` —
  предел тела (по умолчанию теперь 10 МиБ вместо 1 МиБ).
- HEAD-запрос получает заголовки GET-ответа без тела.
- Обработка запроса вынесена из сокетного кода в `src/core/HttpServer.cpp` и
  покрыта тестами без сети.
- Пример [examples/website](examples/website): гостевая книга из трёх файлов с
  шаблонами, статикой, формой, cookies, загрузкой и скачиванием файлов и JSON API.

### Шаблоны и JSON

- `template_render(template, json)`: `{{путь}}` с экранированием HTML, `{{{путь}}}`,
  `{{#each}}` с `{{.}}`, `{{@index}}`, `{{@key}}`, `{{#if}}...{{else}}...{{/if}}`,
  комментарии.
- Сборка JSON без склейки строк: `json_set(json, path, value)`, `json_set_raw`,
  `json_value(value)`, проверка `json_valid(json)`.
- `url_encode`, `url_decode`, `html_escape`.
- `fail(message)` — явная ошибка выполнения со своим сообщением; в обработчике
  HTTP-запроса она даёт ответ 500.

### Проекты из нескольких файлов

- Языковой сервер и `foxlang check` анализируют файл вместе с остальными файлами его
  программы: теми, что его подключают, и всем, что подключают они. Функции и
  переменные из соседних файлов больше не подсвечиваются красным как необъявленные,
  переход к определению открывает другой файл, правка одного открытого файла
  обновляет диагностику остальных. Независимые программы с общей библиотекой друг
  друга не видят. Несохранённые правки учитываются.
- Языковой сервер правильно декодирует пути с пробелами и кириллицей в `file://`
  URI: раньше в такой папке не находились подключаемые файлы, и все их функции
  считались необъявленными.
- Массивы, объявленные в модулях (`array names;`), известны анализатору.
- Ошибки указывают файл: `FoxLang: lib/utils.fox:12: Runtime Error: ...` вместо
  `[line 12]`, причём файл того модуля, где ошибка случилась. После возврата из
  функции позиция возвращается к вызывающей строке. Синтаксические ошибки выглядят так
  же: `main.fox:3: Syntax Error: expected ';' but found end of file`.
- `FOXLANG_ABSOLUTE_PATHS=1` печатает полные пути в сообщениях об ошибках.

### Редакторы

- VS Code: кнопка ▶ в заголовке редактора и `Ctrl+Alt+R`, команды «запустить»,
  «проверить без запуска», «собрать приложение» и «сделать этот файл главным» для
  проектов из нескольких файлов, настройки `foxlang.run.*`, тип задач `foxlang` для
  `tasks.json`. Ошибки проверки и выполнения попадают в панель «Проблемы».
- Kate: внешние инструменты «FoxLang: запустить / проверить / собрать приложение»,
  устанавливаются `install.sh`.
- Zed: задачи запуска, проверки и сборки.

### Графика

- На Linux клавиша `'` больше не срабатывает как стрелка вправо: её keysym совпадал с
  кодом `RIGHT`.
- Клавиши `SHIFT`, `CTRL`, `ALT`.
- `draw_line`, `draw_frame` (контур), `draw_ring` (окружность) и `text_width` для
  выравнивания текста.
- Шрифт рисует `% " ' * # < > @ & { } | $ ^ ~ \ № ° « » — × …` вместо `?`.

## FoxLang 6.0.0

Выпуск чистит язык от наследия и делает документацию, подсказки редактора и рантайм
одним целым. Часть старых функций удалена — таблица замен ниже.

### Язык

- Встроенные функции стали обычными именами: парсер больше не держит для них 30
  отдельных ключевых слов. Поэтому `print(a, b, c)`, `input("Имя: ")`, `size("текст")`
  и вызов любой встроенной функции внутри выражения работают так, как было описано
  в документации, а раньше давали синтаксическую ошибку.
- Массивы: литералы `[1, 2, 3]`, индексы `items[i]` и `items[i] += 1`, пустой массив
  `array items;`, объявление из значения `array parts = str_split(...)`, тип `array` у
  параметров и результата функций, `global array`. Новые `push`, `pop`, `insert`,
  `remove_at`, `resize`. `print` выводит массив как `[1, 2, 3]`.
- Массив передаётся в функцию по ссылке, копируется при присваивании и при возврате
  из функции. Временные массивы (результаты `str_split`, литералы) освобождаются
  вместе со своей областью; раньше каждый результат `str_split` жил до конца программы,
  а массив, возвращённый из функции, указывал на уже освобождённую память.
- Одно правило приведения типов для переменных, параметров и результатов функций:
  параметр `int` больше не принимает строку молча, `float half(int v)` возвращает
  `float`, а функция без `return` при непустом типе результата — ошибка.
- Операторы `--` и `%=`; `!` и унарный минус проверяют тип операнда; `-2147483648`
  теперь допустимый литерал.
- `for (;;)` работает (раньше пустое условие было ошибкой типа), условие `for` может
  содержать `&&` и `||`.
- `switch`: сквозной переход без `break` доходит и до `default`, `default` обязан
  быть последним, числовые `case` сравниваются по значению, `continue` внутри `switch`
  продолжает цикл.
- Строки: escape-последовательности `\uXXXX`, `\u{1F98A}`, суррогатные пары и `\0`.
- `exit(code)` завершает программу с кодом возврата; `return` вне функции — понятная
  ошибка, а не аварийное завершение процесса.
- Аргументы командной строки: `foxlang app.fox a b` и `./app a b` передают программе
  `["a", "b"]` через `os_args()`.
- Одноимённые встроенная функция и функция FoxLang уживаются по понятному правилу:
  встроенная вызывается, когда подходят её аргументы. Так `get(items, i)` работает
  рядом с `get(path, handler)` из модуля `server` без особых случаев в рантайме.

### Встроенные функции и стандартная библиотека

- Единый каталог встроенных функций (`src/core/builtins/`): сигнатура, модуль и
  документация хранятся рядом с реализацией. Рантайм проверяет по нему число и типы
  аргументов с именем параметра в сообщении, анализатор — вызовы в `foxlang check`,
  языковой сервер берёт из него подсказки, а подсветка редакторов генерируется из него.
- Новые функции: `type_of`, `to_int`, `to_float`, `to_string`, `is_number`,
  `random_float`, `tan`, `asin`, `acos`, `atan`, `atan2`, `exp`, `log`, `log10`,
  строковые `str_join`, `str_upper`, `str_lower`, `str_trim`, `str_starts_with`,
  `str_ends_with`, `str_index_of`, `str_substring`, `str_repeat`, JSON `json_count` и
  `json_type`, массивы `array_sort`, `array_reverse`, `array_index_of`, `array_copy`,
  `array_slice`, `env_set`, `clock_ms`, `time_format`.
- Новые модули: `arrays` (сортировка, поиск, срезы, `sum`, `range`), `fs` (проверка
  путей, каталоги, удаление, `read_lines`, `write_lines`), `os` (аргументы, платформа,
  рабочий каталог).
- Расширены `string` (`split`, `join`, `upper`, `lower`, `trim`, `substring`,
  `index_of`, `pad_left` и другие — все в символах, с кириллицей), `math` (`PI`, `E`,
  `degrees`, `lerp`, `sign`, `is_even`), `json` (`json_quote`, `json_count_at`,
  `json_type_at`), `time` (`uptime_ms`, `format_time`, `now_text`), `env` (`set_env`).
- JSON-пути понимают индексы массивов (`items.0.name`, `items[0].name`); разбор
  переписан, некорректный JSON просто не содержит значения.
- HTTP-клиент: `http_request` для любого метода и `http_status()`. Ответ возвращается
  при любом коде статуса — раньше GET и POST при 404 давали пустую строку, а PUT и
  DELETE — тело, и отличить ошибку было нельзя. Модуль `http`: `http_get`,
  `http_post`, `http_put`, `http_patch`, `http_delete`, `http_ok`.
- HTTP-сервер: маршруты для любого метода (`put`, `delete`, `route`), `query()`,
  `header(name)`, `respond_as` с выбором `Content-Type`, правильные reason-фразы
  статусов. Состояние сервера больше не хранится в скрытых переменных программы.
- `read_file` возвращает файл байт в байт: раньше к каждой строке добавлялся перевод
  строки, в том числе к последней.
- `str_split` с пустым разделителем делит строку на символы, а не на байты UTF-8.
- Документация функций стандартной библиотеки хранится в самих `std/*.fox`
  (комментарии `//!` и `///`) и показывается в редакторе.

### Удалено

| Было | Стало |
|---|---|
| `httpget(url)`, `http_get(url)` (встроенная), `http_fetch(url)` | `http_get(url)` из `using http;` |
| `httppost(url, data[, type])`, `http_post_as(...)` | `http_post(url, body, type)`, `http_post_json(url, body)` |
| `httpput(url, data[, type])` | `http_put(url, body, type)`, `http_put_json(url, body)` |
| `httpdelete(url)`, `http_remove(url)` | `http_delete(url)` |
| `server_start`, `server_start_tls`, `route_get`, `route_post`, `send_response` | `listen`, `listen_tls`, `get`, `post`, `respond` из `using server;` или `server_listen`, `server_route`, `server_respond` |
| `src/net.fox` (`start_server`, `register_get`, `json_response`) | модуль `server` |
| `str_to_int(text)`, `strtoint(text)` (возвращали 0 на мусоре) | `to_int(text)` с проверкой `is_number(text)` |
| `min_int`, `max_int` | встроенные `min`, `max` (возвращают `int` для двух `int`) |
| `time_ms()` (строка) | `time_now_ms()` / `unix_time_ms()` (`float`), `clock_ms()` для замеров |
| `readfile(path)` | `read_file(path)` |
| `fox()` | — |
| `FOXLANG_LOG=false` | `FOXLANG_LOG_LEVEL=off` |

### Инструменты и выпуск

- Версия хранится только в `VERSION`: CMake сам проставляет её в манифесты VS Code и
  Zed (без сборки — `cmake -P cmake/Version.cmake`), а тест не даёт повторить номер в
  документации. Выпуск публикует `foxlang.vsix` без номера версии, чтобы ссылка на
  последний выпуск не менялась.
- `foxlang check` проверяет число аргументов встроенных функций, `return` вне функции
  и без значения, а функции можно вызывать до их объявления в файле. Модули для
  анализа ищутся так же, как при запуске, включая встроенную stdlib.
- Документация переписана под фактический язык. Каждый блок кода в README и docs и
  каждый пример из `examples/` проверяется `foxlang check` в CTest, а тест требует,
  чтобы DOCUMENTATION.md описывал каждую встроенную функцию и каждую функцию `std`.
- Новые примеры: игра, частотный словарь, список дел, HTTP-клиент, REST API,
  Telegram-бот на long polling, терминал и графика; устаревшие удалены.
- Интерпретатор примерно на треть быстрее на вычислительных циклах: числовые
  литералы разбираются один раз, встроенная функция находится один раз на узел вызова.
- Синтаксические ошибки называют ожидаемое и найденное: `expected ';' but found end of
  file at line 3`.

## FoxLang 5.7.0

- Числа хранят двоичную форму за своим текстовым представлением: арифметика больше
  не разбирает и не форматирует строку на каждом промежуточном результате. Текст
  вычисляется, только когда его действительно читают. Целочисленный цикл ускорился
  с 1.65 до 1.23 с, дробная физика — с 1.16 до 0.39 с.
- Блок `{ ... }` наконец создаёт область видимости, как и было описано в документации:
  переменная из цикла или ветки `if` снаружи не видна, счётчик `for` принадлежит циклу,
  а повторное объявление имени в той же области — ошибка. Затенение внешнего имени
  во вложенной области разрешено.
- Встроенные `sqrt`, `pow`, `sin`, `cos`, `floor`, `ceil` и `str_length`; в `std/math.fox`
  добавлены `hypot` и `radians`, в `std/string.fox` — `length`. Каталог редакторов,
  подсказки LSP и документация синхронизированы.

- `float` больше не теряет точность: результат операции хранится кратчайшей записью,
  которая читается обратно в то же число, вместо шести знаков после запятой.
- `int` проверяется на диапазон в объявлении, присваивании и арифметике. Переполнение
  и слишком большой литерал дают понятную ошибку вместо тихого заворота и `stoi`.
- Ошибки выполнения содержат номер строки: `... [line N]`.
- Глубина вызовов ограничена доступным стеком потока: бесконечная рекурсия завершается
  ошибкой, а не падением процесса. Windows-сборки резервируют те же 8 МБ стека, что
  Linux даёт по умолчанию.
- Функция видит глобальные переменные, но не локальные переменные вызывающей функции.
- `&&` и `||` вычисляют правую часть только при необходимости; условия и операнды
  логических операторов обязаны быть `bool`.
- Память массивов освобождается при выходе из области объявления и при повторном
  объявлении в цикле. Раньше буферы копились в корневом контексте до конца программы.
- Новая команда `foxlang check <file.fox>`: диагностики семантического анализатора
  в терминале и CI, без запуска программы.
- `random(min, max)` проверяет порядок границ вместо неопределённого поведения.
- Графика на Linux: латинские клавиши находятся во всех группах раскладки, поэтому
  управление работает и при активной русской раскладке.

## FoxLang 5.6.1

- Нативный модуль `graphics`: программное 2D-рисование, встроенный шрифт с кириллицей,
  клавиатура и мышь. XCB на Linux, Win32/GDI на Windows; общий runtime для CLI и standalone.
- Статическая графика в переносимом Linux executable и тест настоящего окна в CI.
- Документация standalone и происхождения CA переведена на русский.
- LSP: дополнение `graphics`, полный каталог встроенных функций, исправленные
  типы возвращаемых значений, справка ключевых слов и операторов, подсказки
  аргументов после Unicode, внутри вложенных вызовов, со строками и комментариями.
- LSP на Windows использует бинарный stdio: CRT больше не изменяет байты
  заголовков Content-Length и JSON-RPC сообщений.
- Подсветка VS Code, Kate и Zed синхронизируется с реальными функциями runtime
  и stdlib; удалены названия отсутствующих функций. Добавлены проверки каталога
  и всех 11 std-модулей через настоящий LSP-процесс.
- Версии CLI/LSP, VSIX, Zed и актуальной документации синхронизированы до 5.6.1.

## FoxLang 5.6.0

- Встроенный HTTPS-сервер `listen_tls(port, certificate, private_key)` на Linux и
  Windows, без обязательного reverse proxy. PEM credentials читаются при запуске;
  отсутствующий/неверный ключ не приводит к переключению на незашифрованный HTTP.
- Публичный набор CA Mozilla встроен и проверяется по закреплённому SHA-256.
  Системный пакет CA больше не обязателен; доступны явные `embedded`, `system`
  и пользовательский PEM через `FOXLANG_CA_BUNDLE`.
- Переносимый Linux-пакет собирается статически на musl. CI проверяет отсутствие
  ELF interpreter/shared libraries и запуск Alpine-сборки на Ubuntu.

- `foxlang build file.fox [-o|--output app]` создаёт один standalone executable
  со встроенным runtime, stdlib и рекурсивными локальными модулями для Linux x86_64
  и Windows x86_64. Компилятор при упаковке и установленный FoxLang у получателя
  не нужны. Это runtime bundling, не native AOT-компиляция.
- Bundle использует проверяемый ELF/PE-дескриптор, версию, границы, таблицу модулей
  и CRC32; существующий выходной файл не перезаписывается. `.env`, значения
  окружения и ресурсы автоматически не упаковываются.
- HTTP(S)-клиент больше не запускает внешний `curl`: libcurl встроен статически,
  TLS использует Mbed TLS на Linux и Schannel на Windows. Сертификаты и имя
  сервера проверяются; поддержан runtime `FOXLANG_CA_BUNDLE`.
- HTTP-сервер, TCP и DNS работают через общее ядро с POSIX sockets и Winsock.
  Исправлены частичная отправка данных, регистр HTTP-заголовков, проверка
  Content-Length и HTTP 500 при исключении обработчика.
- Версии CLI/LSP берутся из VERSION; метаданные VS Code и Zed синхронизированы.
  VSIX-сборщик проверяет совпадение версий и переносимые пути архива.
- Добавлены изолированные standalone-тесты, локальные HTTP/HTTPS/TCP и webhook
  проверки, тесты повреждённого bundle, отсутствия секретов и согласованности
  версий. Linux и Windows выполняют их в обычном CI.

# FoxLang 5.5.3 — Changelog

- **Функция `env_default(name, fallback)`**:
  - Добавлена функция чтения переменных окружения со значением по умолчанию в рантайм `Runtime.cpp` и модуль `std/env.fox`.
  - Добавлен алиас `strtoint(text)` в `std/string.fox`.
- **Первоклассная справка и подсказки в редакторах (VS Code & Kate)**:
  - **Signature Help (`textDocument/signatureHelp`)**: при вводе скобки `(` или запятой `,` отображается интерактивная подсказка сигнатуры с подсветкой текущего активного параметра в стиле Kotlin и Zig.
  - **Богатое автодополнение (Rich Completions)**: полная Markdown-документация с примерами кода для ключевых слов (`if`, `while`, `for`, `switch`, `using`, `include`, `secret` и др.), модулей и всех встроенных функций.
  - **Информативные Hover-подсказки**: всплывающие окна с точными сигнатурами `(function) name(args...) -> returnType`, описанием параметров, типов и примерами.
  - **Точная синхронизация стандартной библиотеки**: функции всех 10 модулей (`server`, `http`, `env`, `log`, `json`, `string`, `math`, `net`, `terminal`, `time`) приведены в полное соответствие с `std/*.fox`.
- **Контейнеризация и GHCR**:
  - Официальный многоэтапный Dockerfile на базе Alpine Linux (~29 МБ) с предустановленными `foxlang`, `foxlang-lsp` и `std/`.
  - Автоматическая сборка и публикация в GitHub Container Registry (`ghcr.io/skrinvex/foxlang`) через GitHub Actions.
  - Подготовка VSIX-пакета `foxlang-5.5.3.vsix`.

---

# FoxLang 5.5.2 — Changelog

- **Улучшенная подсветка синтаксиса**:
  - VS Code: обновлена TextMate-грамматика — поддержаны многострочные комментарии `/* ... */`, теги `TODO:` / `@param`, подсветка директив модулей `using`/`include`, объявления и параметры функций, шестнадцатеричные числа, строковые escape-последовательности `\uXXXX` и полный список функций стандартной библиотеки (`secret`, `listen`, `respond`, `info`, `warn`, `error` и др.).
  - Kate: обновлена XML-схема KSyntaxHighlighting — добавлены блочные комментарии, сворачивание кода (folding блоков `{}`), Unicode-экранирование и встроенные функции.
- **Автономный LSP-клиент для VS Code (Zero-Dependency)**:
  - Клиент расширения переписан на чистый VS Code Extension Host API и встроенный Node.js без внешних npm-зависимостей (`vscode-languageclient`). Расширение работает сразу после установки без `npm install`.
- **Надёжная установка расширения VS Code**:
  - Исправлено имя каталога расширения на `<publisher>.<name>-<version>` (`foxlang.foxlang-5.5.2`) в `install.sh`.
  - Добавлена автоматическая сборка и установка пакетов `.vsix` (`code --install-extension`).
  - Добавлена очистка записей `.obsolete` при переустановке.
  - В артефакты релиза включён официальный готовый файл `foxlang-5.5.2.vsix`.

---

# FoxLang 5.5.1 — Changelog

- **Автоматическая настройка редакторов**: скрипт `install.sh` теперь автоматически устанавливает подсветку синтаксиса `foxlang.xml` и настраивает LSP для Kate, а также подготавливает расширение для VS Code.
- **Удобная загрузка из релизов**: в артефакты каждого релиза добавлены отдельные готовые файлы: `foxlang.xml` (для Kate) и `foxlang-vscode.zip` (для VS Code).
- **Обновление документации**: в главном `README.md`, `docs/EDITORS.md` и руководствах редакторов добавлены прямые ссылки на конфигурационные файлы и подробные пошаговые инструкции.

---

# FoxLang 5.5.0 — Changelog

- **Архитектурный рефакторинг ядра**: разделение на переиспользуемую статическую библиотеку `foxlang_core` и тонкий CLI-интерпретатор `foxlang`.
- **Языковой сервер (LSP)**: добавлен автономный сервер `foxlang-lsp` с реализацией протокола JSON-RPC 2.0 (stdio) без внешних зависимостей.
- **Статический семантический анализ**: реализован `SemanticAnalyzer`, выполняющий проверку типов, областей видимости и валидацию AST без исполнения пользовательского кода.
- **Инструменты разработки (IDE & редакторы)**:
  - Поддержка **Kate (KDE)**: XML-схема подсветки синтаксиса KSyntaxHighlighting и конфигурация для плагина LSP Client.
  - Расширение для **Visual Studio Code**: TextMate-грамматика синтаксиса, конфигурация языка и LSP-клиент.
  - Подробное руководство по интеграции в `docs/EDITORS.md`.
- **Точные координаты исходного кода**: поддержка `SourcePosition`, `SourceRange`, учет UTF-16 code units для корректной работы с эмодзи и кириллицей в LSP.
- **Диагностика**: расширенная программная модель `Diagnostic` с уровнями важности (Error, Warning, Info, Hint) и восстановлением после синтаксических ошибок в парсере.
- **Инсталляция и CI**: расширен `install.sh` для установки `foxlang-lsp`; автоматическая сборка релизных бинарников для Linux и Windows в GitHub Actions.

---

# FoxLang 5.4.5 — Changelog

- JSON parser теперь декодирует `\\uXXXX` в UTF-8, включая surrogate pairs для emoji.
- Добавлен env-флаг `FOXLANG_LOG`: `false`, `0`, `off` или `no` отключают std/log; по умолчанию логи включены.

---

# FoxLang 5.4.4 — Changelog

- Исправлен HTTP POST: JSON body теперь безопасно передаётся curl без разрушения кавычек shell.
- POST получил таймауты, fail-with-body и подробное логирование ошибок curl/HTTP.

---

# FoxLang 5.4.3 — Changelog

- Исправлен конфликт ключевого слова `get` с функцией `get()` из `std/server.fox`.
- Проверены lexer keyword tokens: каждый оставшийся специальный токен имеет обработчик parser.

---

# FoxLang 5.4.1 — Changelog

- JSON helper теперь понимает вложенные пути вроде `message.chat.id` и `message.from.username`.
- Добавлено безопасное экранирование JSON-строк.
- Telegram webhook example стал полноценным ботом: /start, /help, /ping, /about, /id и echo.
- Добавлено подробное логирование входящих update и ответов Telegram API.

---

# FoxLang 5.4.0 — Changelog

- Заглушки HTTP-сервера заменены реальным Linux/POSIX HTTP/webhook runtime.
- Добавлены реальные GET/POST routes, request body/method/path и HTTP response status.
- Добавлен модуль `std/server.fox`.
- Добавлена автоматическая загрузка `.env` и обязательные секреты через `secret()`.
- Добавлен пример Telegram webhook server.
- Исправлено отображение exit code curl на POSIX.

---

# FoxLang 5.3.0 — Changelog

- Исправлен разбор `round()` и `random()`: теперь это обычные runtime-функции, а не зарезервированные токены без рабочего parser path.
- Добавлены стандартные модули `log` и `env`.
- Добавлены `log_debug`, `log_info`, `log_warn`, `log_error` и `env_get`.
- Добавлены core/stdlib smoke-тесты в CI; сломанный runtime больше не должен автоматически уходить в Release.
- Добавлен диагностический пример Telegram Bot API без токена в исходнике.
- Сетевые ошибки HTTP GET теперь видимы в stderr и ограничены таймаутами.

---

# FoxLang 5.2.2 — Changelog

- HTTP GET теперь показывает ошибки curl в stderr вместо молчаливого возврата пустой строки.
- Добавлены таймаут подключения и общий таймаут HTTP-запроса.
- Исправлена русская документация стандартной библиотеки.

---

# FoxLang 5.2.1 — Changelog

- README полностью переработан на русском и приведён к актуальной структуре 5.2.
- Linux Release теперь содержит полноценный пользовательский установщик, бинарник и `std/`.
- Установщик размещает runtime в `~/.local/share/foxlang` и команду в `~/.local/bin/foxlang`.
- Добавлен скрипт удаления установленной версии.
- Release workflow упаковывает готовый Linux SDK/runtime вместо одиночного бинарника.

---

# FoxLang 5.2.0 - Changelog

## Standard library
- Added first-class `std/` layout with `terminal`, `net`, `http`, `math`, `string`, and `time`.
- `using module;` now resolves real FoxLang modules instead of being a no-op.
- Added `FOXLANG_HOME` lookup and import deduplication.

## Networking
- Added real POSIX DNS lookup and TCP client primitives.
- Added FoxLang wrappers: `connect_tcp`, `send_tcp`, `recv_tcp`, `close_tcp`, and `resolve_host`.
- Kept HTTPS HTTP helpers for compatibility.
- Documentation now explicitly marks the old server API as compatibility shims instead of claiming it is a real background server.

## Runtime
- Fixed Linux `kbhit()` so it no longer blocks waiting for input.
- Added reusable terminal primitives.
- Added `abs`, `min`, `max`, `clamp`, and `time_ms` runtime helpers.

## Examples
- Added `examples/tcp_client.fox`.
- Added `examples/stdlib_demo.fox`.

---

# FoxLang 5.0.1 - Changelog

## 🆕 Новые возможности

### Встроенная функция read_file()
- **Чтение файлов:** Новая функция `read_file(filename)` для чтения текстовых файлов
- **Умная обработка:** Автоматически пропускает комментарии (строки начинающиеся с #) и пустые строки
- **Безопасность:** Возвращает пустую строку при ошибке чтения файла
- **Применение:** Идеально подходит для чтения конфигурационных файлов

### Встроенная функция http_get()
- **HTTP запросы:** Новая функция `http_get(url)` для выполнения GET запросов
- **Простота использования:** Один вызов функции для получения данных с сервера
- **Интеграция:** Использует системный curl для надежности
- **Применение:** Работа с API, загрузка данных, веб-скрапинг

```cpp
string config = read_file("config.txt");
if (config != "") {
    print("Config loaded: " + config);
}
```

---

# FoxLang 5.0.0 - Changelog

## 🚀 Крупные нововведения

### 1. Пользовательские функции
- **Полная поддержка функций** с параметрами и возвратом значений
- **Локальные области видимости** для параметров функций
- **Рекурсивные вызовы** функций
- **Поддержка всех типов** в параметрах и возврате (`int`, `float`, `string`, `bool`, `void`)

```cpp
int factorial(int n) {
    if (n <= 1) {
        return 1;
    }
    return n * factorial(n - 1);
}
```

### 2. Модульная система с пользовательскими функциями
- **Библиотеки с функциями**: Теперь `include("lib.fox")` поддерживает пользовательские функции
- **Правильная область видимости**: Функции из библиотек доступны в основном коде
- **Режим импорта**: Функции регистрируются, но код библиотеки не выполняется

```cpp
// math_lib.fox
int add(int a, int b) {
    return a + b;
}

// main.fox
include("math_lib.fox");
int result = add(5, 3); // Работает!
```

### 3. Современный синтаксис идентификаторов
- **Поддержка подчеркиваний**: `user_name`, `get_public_ip`, `my_function`
- **Исправлен лексер**: Идентификаторы с `_` не конфликтуют с ключевыми словами
- **Совместимость**: Все существующие имена переменных продолжают работать

### 4. Расширенная поддержка циклов
- **Цикл for**: Полная поддержка `for (init; condition; step) { ... }`
- **Улучшенные while циклы**: Лучшая обработка условий
- **Вложенные циклы**: Поддержка любого уровня вложенности

## 🔧 Технические улучшения

### Парсер и AST
- **Новые узлы**: `FuncDefNode`, `ReturnNode`, `ForNode`
- **Улучшенная обработка**: `return` внутри функций
- **Контекст функций**: Изолированные области видимости
- **Обработка ошибок**: Лучшие сообщения об ошибках

### Лексер
- **Исправлена логика**: Идентификаторы с подчеркиваниями обрабатываются корректно
- **Приоритет ключевых слов**: Только полные совпадения считаются ключевыми словами

## 📋 Примеры использования

### Пользовательские функции
```cpp
string greet(string name, int age) {
    return "Hello, " + name + "! You are " + age + " years old.";
}

void main() {
    string message = greet("Alice", 25);
    print(message);
}

main();
```

### Библиотеки с функциями
```cpp
// utils.fox
int max(int a, int b) {
    if (a > b) return a;
    return b;
}

void print_array(array arr, int size) {
    int i = 0;
    while (i < size) {
        print("arr[" + i + "] = " + get(arr, i));
        i = i + 1;
    }
}

// main.fox
include("utils.fox");

int maximum = max(10, 20);
array numbers 3;
set(numbers, 0, 5);
set(numbers, 1, 10);
set(numbers, 2, 15);
print_array(numbers, 3);
```

### Современный синтаксис
```cpp
string user_name = "john_doe";
int user_age = 25;
bool is_admin = false;

string get_user_info() {
    return "User: " + user_name + ", Age: " + user_age;
}

void check_permissions() {
    if (is_admin) {
        print("Admin access granted");
    } else {
        print("Regular user access");
    }
}
```

## 🔄 Обратная совместимость
- Все скрипты FoxLang 4.x продолжают работать
- Встроенные функции (`print`, `input`, `fox`) не изменились
- Синтаксис массивов и переменных остался прежним

## 🐛 Исправленные ошибки
- Исправлена обработка идентификаторов с подчеркиваниями
- Устранены конфликты ключевых слов с именами функций
- Улучшена стабильность парсера при обработке функций
- Исправлены ошибки области видимости переменных

---

**Версия 5.0.0** представляет собой значительный шаг вперед в развитии FoxLang, превращая его в полноценный язык программирования с поддержкой пользовательских функций и современной модульной системы.
