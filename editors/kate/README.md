# Skrumi для текстового редактора Kate (KDE)

Интеграция языка Skrumi с редактором Kate: подсветка синтаксиса и полная поддержка Language Server Protocol (LSP).

Файлы конфигурации в этом каталоге:
* [`skrumi.xml`](skrumi.xml) — XML-схема подсветки синтаксиса (KSyntaxHighlighting).
* [`settings.json`](settings.json) — готовый фрагмент конфигурации для плагина LSP Client.
* [`externaltools/`](externaltools/) — внешние инструменты «Skrumi: запустить», «проверить» и «собрать приложение».
* [`dap.json`](dap.json) — настройки плагина «Отладчик» для `skrumi debug-adapter`.

Графический установщик предлагает интеграцию для обнаруженного Kate:
подсветку, внешние инструменты, настройки LSP и отладчика. Нужные плагины Kate
включаются в его настройках. Ручная установка описана ниже; существующие настройки
редактора сохраняются.
Старый `install.sh` исторических пакетов автоматизировал эти действия.

## Запуск, проверка и сборка

Инструменты из [`externaltools/`](externaltools/) появляются в меню **Сервис → Внешние
инструменты → Skrumi**. Они сохраняют все документы, запускают `skrumi` в папке
текущего файла и показывают вывод в нижней панели. Установка из исходников репозитория:

```bash
mkdir -p ~/.config/kate/externaltools
cp editors/kate/externaltools/*.ini ~/.config/kate/externaltools/
```

Кнопку запуска можно вынести на панель инструментов (**Настройка → Панели
инструментов**) и назначить ей клавишу (**Настройка → Комбинации клавиш**).

## Отладка

Включите плагин **Отладчик** (Настройка → Настроить Kate → Модули) и положите
[`dap.json`](dap.json) в `~/.config/kate/debugger/dap.json`, если файла ещё нет;
иначе добавьте секцию `skrumi` в свой файл, сохранив остальные профили:

```bash
mkdir -p ~/.config/kate/debugger
cp editors/kate/dap.json ~/.config/kate/debugger/dap.json
```

На панели отладчика выберите **skrumi → launch (debug)** или **launch (stop on entry)**.
Точки останова, шаги, стек, переменные и консоль работают как в других отладчиках Kate.
Вывод программы появляется на панели отладчика, `input()` получает пустую строку.
Подробнее — [docs/DEBUGGING.md](../../docs/DEBUGGING.md).

## 1. Подсветка синтаксиса (KSyntaxHighlighting)

Kate использует формат KSyntaxHighlighting XML для распознавания языков и правил раскраски кода.

### Ручная установка подсветки:
Скопируйте [`skrumi.xml`](skrumi.xml) в каталог пользовательских синтаксисов KDE:

```bash
mkdir -p ~/.local/share/org.kde.syntax-highlighting/syntax/
cp editors/kate/skrumi.xml ~/.local/share/org.kde.syntax-highlighting/syntax/
```

Файл также доступен для прямой загрузки со страницы релизов: [`skrumi.xml`](https://github.com/SkrinVex/Skrumi/releases/latest/download/skrumi.xml).

После этого файлы с расширением `.skm` будут автоматически распознаваться с подсветкой синтаксиса Skrumi (ключевые слова, типы, строки, числа, встроенные функции stdlib).

---

## 2. Подключение Language Server (`skrumi-lsp`) в Kate

В Kate встроен мощный LSP-клиент (плагин **LSP Client** / **Клиент LSP**).

### Шаг 1. Включение плагина LSP Client в Kate
1. Откройте Kate.
2. Перейдите в **Settings** -> **Configure Kate...** -> вкладка **Plugins** (Плагины).
3. Установите галочку напротив **LSP Client** (Клиент LSP) и нажмите **Apply**.

### Шаг 2. Добавление конфигурации `skrumi-lsp`
1. Перейдите в **Settings** -> **Configure Kate...** -> раздел **LSP Client** -> вкладка **User Server Settings** (Пользовательские настройки сервера).
2. Либо отредактируйте файл конфигурации напрямую:
   ```bash
   mkdir -p ~/.config/kate/lspclient
   ```
   Добавьте сервер `skrumi` в `~/.config/kate/lspclient/settings.json`:
   ```json
   {
       "servers": {
           "skrumi": {
               "command": ["skrumi-lsp"],
               "root": "",
               "url": "https://github.com/SkrinVex/Skrumi",
               "highlightingModeRegex": "^Skrumi$"
           }
       }
   }
   ```
   *Примечание: Если бинарник `skrumi-lsp` ещё не добавлен в системный `PATH`, укажите абсолютный путь к нему, например: `["/home/user/Skrumi/build/skrumi-lsp"]`.*

3. Нажмите **Apply** / Сохранить.

---

## 3. Доступные возможности в Kate

После настройки при открытии любого `.skm` файла в нижней строке состояния появится значок активного LSP-сервера `skrumi-lsp`:
* **Ошибки и предупреждения (Diagnostics)**: подчёркивание ошибок в реальном времени, всплывающие подсказки и вкладка **Diagnostics** в нижней панели Kate.
* **Автодополнение (Auto-completion)**: список ключевых слов, функций и переменных при наборе.
* **Подсказки при наведении (Hover)**: всплывающее окно с сигнатурой функции или типом переменной.
* **Переход к определению (Go to Definition)**: клавиша `F12` или контекстное меню правой кнопки мыши -> *Go to Definition*.
* **Символы документа**: панель навигации по функциям и переменным файла.
 
---

Подробное описание архитектуры и возможностей языкового сервера приведено в документе [`docs/EDITORS.md`](../../docs/EDITORS.md).
