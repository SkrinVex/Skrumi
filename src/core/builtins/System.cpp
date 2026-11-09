#ifdef __ANDROID__
#include "skrumi/Android.h"
#endif
// Builtins that talk to the operating system: files, environment, logging, time,
// process information and the terminal.
#include "Builtin.h"
#include "skrumi/Skrumi.h"
#include <algorithm>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace skrumi::runtime {
namespace {
namespace fs = std::filesystem;

// Paths are UTF-8 text in Skrumi; Windows needs them widened, not reinterpreted.
fs::path pathOf(const std::string& text) { return platform::pathFromUtf8(text); }

Value writeText(Call& c, std::ios::openmode mode) {
    std::ofstream file(pathOf(c.text(0)), std::ios::binary | mode);
    if (!file.is_open()) return boolean(false);
    file << c.text(1);
    file.close();
    return boolean(static_cast<bool>(file));
}

Value logLine(Call& c, int level, const char* label) {
    if (level >= getLogLevelThreshold()) platform::writeOutput(std::string("[") + label + "] " + c.text(0) + "\n", true);
    return nothing();
}

Value terminal(const char* sequence) {
    platform::writeOutput(sequence);
    return nothing();
}

const auto started = std::chrono::steady_clock::now();

} // namespace

void addSystemBuiltins(std::vector<Builtin>& out) {
    auto add = [&](BuiltinSpec spec, Handler handler) { out.push_back({std::move(spec), handler}); };

    // Files
    add({"read_file", "string", {{"string", "path"}}, 1, false, "",
         "Содержимое файла целиком, байт в байт. Если файл не открывается — пустая строка; "
         "проверить наличие можно через `exists` из `using fs;`.\n\n```skrumi\nstring config = read_file(\"config.json\");\n```"},
        [](Call& c) {
            std::ifstream file(pathOf(c.text(0)), std::ios::binary);
            if (!file.is_open()) return text("");
            std::ostringstream content;
            content << file.rdbuf();
            return text(content.str());
        });
    add({"write_file", "bool", {{"string", "path"}, {"string", "content"}}, 2, false, "",
         "Записывает строку в файл, заменяя прежнее содержимое. `true` при успехе."},
        [](Call& c) { return writeText(c, std::ios::trunc); });
    add({"append_file", "bool", {{"string", "path"}, {"string", "content"}}, 2, false, "",
         "Дописывает строку в конец файла, создавая его при необходимости. `true` при успехе."},
        [](Call& c) { return writeText(c, std::ios::app); });
    add({"fs_exists", "bool", {{"string", "path"}}, 1, false, "fs", "Существует ли файл или каталог."},
        [](Call& c) {
            std::error_code ec;
            return boolean(fs::exists(pathOf(c.text(0)), ec));
        });
    add({"fs_is_dir", "bool", {{"string", "path"}}, 1, false, "fs", "Является ли путь каталогом."},
        [](Call& c) {
            std::error_code ec;
            return boolean(fs::is_directory(pathOf(c.text(0)), ec));
        });
    add({"fs_make_dir", "bool", {{"string", "path"}}, 1, false, "fs",
         "Создаёт каталог вместе с недостающими родительскими. `true`, если каталог теперь существует."},
        [](Call& c) {
            std::error_code ec;
            auto path = pathOf(c.text(0));
            fs::create_directories(path, ec);
            return boolean(fs::is_directory(path, ec));
        });
    add({"fs_remove", "bool", {{"string", "path"}}, 1, false, "fs",
         "Удаляет файл или **пустой** каталог. `true`, если что-то было удалено."},
        [](Call& c) {
            std::error_code ec;
            return boolean(fs::remove(pathOf(c.text(0)), ec));
        });
    add({"fs_list", "array", {{"string", "path"}}, 1, false, "fs",
         "Имена файлов и каталогов внутри каталога, по алфавиту. Для отсутствующего каталога — пустой массив."},
        [](Call& c) {
            std::vector<std::string> names;
            std::error_code ec;
            for (fs::directory_iterator it(pathOf(c.text(0)), ec), end; !ec && it != end; it.increment(ec))
                names.push_back(it->path().filename().u8string());
            std::sort(names.begin(), names.end());
            ValueList items;
            for (auto& name : names) items.push_back(text(std::move(name)));
            return makeArray(std::move(items));
        });
    add({"fs_size", "float", {{"string", "path"}}, 1, false, "fs",
         "Размер файла в байтах или `-1`, если это не файл. Тип `float`: размеры больше 2 ГиБ не помещаются в `int`, "
         "а `float` хранит их точно."},
        [](Call& c) {
            std::error_code ec;
            auto size = fs::file_size(pathOf(c.text(0)), ec);
            if (ec) return real(-1);
            return real(static_cast<double>(size));
        });

    // Environment
    add({"env_get", "string", {{"string", "name"}}, 1, false, "env",
         "Значение переменной окружения или пустая строка."},
        [](Call& c) { return text(platform::getEnvVar(c.text(0))); });
    add({"env_set", "bool", {{"string", "name"}, {"string", "value"}}, 2, false, "env",
         "Задаёт переменную окружения текущего процесса. `true` при успехе."},
        [](Call& c) { return boolean(!c.text(0).empty() && platform::setEnvVar(c.text(0), c.text(1))); });
    add({"env_required", "string", {{"string", "name"}}, 1, false, "env",
         "Значение обязательной переменной; если она не задана или пуста — программа завершается с ошибкой."},
        [](Call& c) {
            std::string value = platform::getEnvVar(c.text(0));
            if (value.empty()) throw std::runtime_error("Environment Error: required secret '" + c.text(0) + "' is not set");
            return text(value);
        });

    // Logging to stderr, filtered by SKRUMI_LOG_LEVEL
    add({"log_debug", "void", {{"string", "message"}}, 1, false, "log", "Сообщение уровня `[DEBUG]` в stderr."},
        [](Call& c) { return logLine(c, 10, "DEBUG"); });
    add({"log_info", "void", {{"string", "message"}}, 1, false, "log", "Сообщение уровня `[INFO]` в stderr."},
        [](Call& c) { return logLine(c, 20, "INFO"); });
    add({"log_warn", "void", {{"string", "message"}}, 1, false, "log", "Сообщение уровня `[WARN]` в stderr."},
        [](Call& c) { return logLine(c, 30, "WARN"); });
    add({"log_error", "void", {{"string", "message"}}, 1, false, "log", "Сообщение уровня `[ERROR]` в stderr."},
        [](Call& c) { return logLine(c, 40, "ERROR"); });

    // Time
    add({"time_now_ms", "float", {}, 0, false, "time",
         "UNIX-время в миллисекундах. Тип `float`: число больше диапазона `int`, но хранится точно."},
        [](Call&) {
            auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            return real(static_cast<double>(now));
        });
    add({"clock_ms64", "float", {}, 0, false, "time", "Монотонные миллисекунды как float; целые миллисекунды точны до 2^53, без 24-дневного переполнения int."},
        [](Call&) { return real(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count()); });
    add({"clock_ms", "int", {}, 0, false, "time",
         "Миллисекунды с запуска программы по монотонным часам. Подходит для замера длительности: "
         "не зависит от перевода системных часов."},
        [](Call&) {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
            return integer(static_cast<long long>(elapsed));
        });
    add({"time_format", "string", {{"string", "pattern"}}, 1, false, "time",
         "Текущее локальное время по шаблону `strftime`: `%Y` год, `%m` месяц, `%d` день, `%H:%M:%S` время.\n\n"
         "```skrumi\nprint(time_format(\"%Y-%m-%d %H:%M\"));\n```"},
        [](Call& c) {
            std::time_t now = std::time(nullptr);
            std::tm local{};
#ifdef _WIN32
            localtime_s(&local, &now);
#else
            localtime_r(&now, &local);
#endif
            char buffer[512];
            size_t length = c.text(0).empty() ? 0 : std::strftime(buffer, sizeof(buffer), c.text(0).c_str(), &local);
            return text(std::string(buffer, length));
        });

    // Immutable standalone resources, separate from source imports and disk paths.
    add({"resource_read", "string", {{"string", "name"}}, 1, false, "resources",
         "Байты встроенного ресурса standalone. Имя относительно --resources с разделителем /. Отсутствие ресурса — Resource Error; доступа к cwd нет."},
        [](Call& c) {
            if (!c.ctx.interpreter) throw std::runtime_error("Resource Error: resources require an interpreter");
            return text(c.ctx.interpreter->getSources().readResource(c.text(0)));
        });
    add({"resource_list", "array", {}, 0, false, "resources", "Отсортированные имена встроенных ресурсов; пустой массив при их отсутствии."},
        [](Call& c) {
            ValueList values;
            if (c.ctx.interpreter)
                for (const auto& name : c.ctx.interpreter->getSources().listResources()) values.push_back(text(name));
            return makeArray(std::move(values));
        });

    // Process
    add({"os_args", "array", {}, 0, false, "os",
         "Пользовательские аргументы после имени программы: `skrumi app.skm a b` и `./app a b` дают `[a, b]`. Системные `--skrumi-*` (и прежние `--skrumi-*`) исключаются; после `--` аргументы передаются буквально."},
        [](Call& c) {
            ValueList items;
            if (c.ctx.interpreter)
                for (const auto& argument : c.ctx.interpreter->getArguments()) items.push_back(text(argument));
            return makeArray(std::move(items));
        });
    add({"os_platform", "string", {}, 0, false, "os", "Операционная система: `android`, `linux`, `windows`, `macos` или `other`."},
        [](Call&) {
#if defined(_WIN32)
            return text("windows");
#elif defined(__APPLE__)
            return text("macos");
#elif defined(__ANDROID__)
            return text("android");
#elif defined(__linux__)
            return text("linux");
#else
            return text("other");
#endif
        });
    add({"user_data_dir", "string", {}, 0, false, "os", "Каталог сохранений Skrumi: LOCALAPPDATA/Skrumi, XDG_DATA_HOME/skrumi (или ~/.local/share/skrumi), Android app files. Создаётся при обращении; Web — ошибка."},
        [](Call&) {
            std::string base;
#ifdef __ANDROID__
            base = android::host().directory();
#elif defined(__EMSCRIPTEN__)
            throw std::runtime_error("File Error: persistent user data directory is unavailable in the browser");
#elif defined(_WIN32)
            base = platform::getEnvVar("LOCALAPPDATA");
            if (base.empty()) throw std::runtime_error("File Error: LOCALAPPDATA is not configured");
            base += "/Skrumi";
#else
            base = platform::getEnvVar("XDG_DATA_HOME");
            if (base.empty() || !pathOf(base).is_absolute()) {
                base = platform::getEnvVar("HOME");
                if (base.empty()) throw std::runtime_error("File Error: HOME is not configured");
                base += "/.local/share";
            }
            base += "/skrumi";
#endif
            auto directory = pathOf(base);
            fs::create_directories(directory);
            return text(platform::pathToUtf8(directory));
        });
    add({"os_cwd", "string", {}, 0, false, "os", "Текущий рабочий каталог процесса."},
        [](Call&) {
#ifdef __ANDROID__
            if (android::current()) return text(android::host().directory());
#endif
            std::error_code ec;
            return text(fs::current_path(ec).u8string());
        });

    // Terminal (ANSI escape sequences)
    add({"term_clear", "void", {}, 0, false, "terminal", "Очищает экран терминала и ставит курсор в начало."},
        [](Call&) { return terminal("\033[2J\033[H"); });
    add({"term_home", "void", {}, 0, false, "terminal", "Ставит курсор в левый верхний угол."},
        [](Call&) { return terminal("\033[H"); });
    add({"term_write", "void", {{"string", "text"}}, 1, false, "terminal", "Выводит текст без перевода строки."},
        [](Call& c) {
            platform::writeOutput(c.text(0));
            return nothing();
        });
    add({"term_goto", "void", {{"int", "row"}, {"int", "col"}}, 2, false, "terminal",
         "Перемещает курсор; строки и столбцы нумеруются с 1."},
        [](Call& c) {
            platform::writeOutput("\033[" + std::to_string(c.amount(0, 10000)) + ";" + std::to_string(c.amount(1, 10000)) + "H");
            return nothing();
        });
    add({"term_hide_cursor", "void", {}, 0, false, "terminal", "Скрывает курсор терминала."},
        [](Call&) { return terminal("\033[?25l"); });
    add({"term_show_cursor", "void", {}, 0, false, "terminal", "Показывает курсор терминала."},
        [](Call&) { return terminal("\033[?25h"); });
    add({"term_color", "void", {{"int", "ansi_code"}}, 1, false, "terminal",
         "Включает ANSI-атрибут текста: 31 красный, 32 зелёный, 33 жёлтый, 34 синий, 36 голубой, 1 жирный."},
        [](Call& c) {
            platform::writeOutput("\033[" + std::to_string(c.amount(0, 255)) + "m");
            return nothing();
        });
    add({"term_reset", "void", {}, 0, false, "terminal", "Сбрасывает цвета и атрибуты текста."},
        [](Call&) { return terminal("\033[0m"); });
}

} // namespace skrumi::runtime
