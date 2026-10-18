# Migration to explicit language contracts

The current release removes three language behaviors from the previous major.
The VERSION file is the authoritative version number.

- Replace implicit float-to-int conversion with to_int(value), including typed
  arrays/maps, fields, function arguments/results, builtin int arguments and
  array indices. This truncates toward zero; use a rounding function first when
  that is the intended behavior. Int-to-float widening remains implicit.
- Replace a sized array declaration with an empty declaration and
  resize(name, count). Existing literals and array parameters are unchanged.
- A Skrumi function now shadows a builtin by name for every argument list.
  There is no runtime fallback based on argument types. After importing server,
  get refers to its route function; use items[index] for array access.
  Use a module alias such as using server as http and http.get for explicit
  module calls. Imports still export global names; aliases do not introduce
  isolated module namespaces.
- Fish PATH integration now writes a native conf.d fragment and supports
  transactional rollback. Existing POSIX/zsh plans can still be removed using
  their recorded destination even if the login shell has changed.

Known narrowing is diagnosed by check/LSP. Untyped parameters, dynamic function
values and container contents still require runtime checks; check is not a proof
that a program cannot fail.

# Переход на самостоятельный Skrumi

## History

Skrumi ранее назывался FoxLang. Совместимость с прежним именем намеренно
удалена в major-релизе 9: сторонней экосистемы пока нет, поэтому старые личные
программы переносятся вручную. История Git, теги и опубликованные релизы сохранены.

## Перенос программ и окружения

- Переименуйте исходники `.fox` в `.skm` и обновите явные пути в `include`,
  `using`, конфигурациях запуска, ссылках и ресурсах. Синтаксис языка не меняется.
- Используйте только `skrumi` и `skrumi-lsp`. Старые команды `foxlang` и
  `foxlang-lsp`, параметры `--foxlang-*` больше не предоставляются.
- Замените переменные `FOXLANG_*` на `SKRUMI_*`, включая `FOXLANG_HOME` →
  `SKRUMI_HOME`. Старые переменные больше не влияют на настройки runtime.
- Перенесите настройки редактора на ID `skrumi`, ключи `skrumi.*` и
  расширение VS Code `SkrinVex.skrumi-language`. Это новая идентичность пакета;
  автоматическая замена старого Marketplace-пакета не предполагается.
- Для C++ используйте `include/skrumi`, namespace `skrumi`, `skrumi_core` и
  параметры CMake `SKRUMI_*`. Пересоберите в новом каталоге сборки.
- Android использует модуль `:skrumi`, пакет `org.skrumi.android`, классы
  `Skrumi`, `SkrumiView`, `SkrumiControls` и свойства Gradle `skrumi*`.
  Приложения с новым application ID устанавливаются отдельно от старых.
- Экспортируйте старые проекты Playground/UI Builder перед обновлением;
  переименуйте исходники и перенесите их в новый проект. Старые ключи
  browser storage и протоколы интеграции больше не являются контрактом.

## Импорты

Единственное расширение исходников — `.skm`. CLI, проверка, форматирование,
standalone-сборка и APK-упаковщик отклоняют другие расширения. Поиск `using name;`
рассматривает только `name.skm`: стандартная библиотека имеет приоритет над
одноимённым локальным модулем. Явные пути не подменяются другими расширениями.
Относительные пути вычисляются от импортирующего файла. Повторные и циклические
импорты используют одну каноническую идентичность модуля. Ошибка выбранного
модуля не запускает поиск запасной реализации. Неразрешённые импорты считаются
ошибками также в `skrumi check` и LSP, а не предупреждениями.

## Установка и сохранность данных

Установщик содержит два исполняемых файла и одну стандартную библиотеку.
Standalone и LSP работают с этой библиотекой; стандартные модули также встроены
в runtime. Android SDK распространяется отдельным AAR, упаковщик APK находится
в репозитории и требует Android SDK/NDK/Gradle.

Не удаляйте неизвестные каталоги или пользовательские файлы. Новая установка
не ищет старые установки по прежнему бренду и не удаляет их автоматически.
Обновление явно выбранной управляемой установки использует манифесты владения,
проверку хешей, журнал, откат и восстановление. Старый установщик следует
удалять его собственным механизмом после сохранения личных программ.
