// Core builtins: output, input, arrays, conversions and math.
#include "Builtin.h"
#include "skrumi/Platform.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <random>
#include <stdexcept>
#include <thread>

namespace skrumi::runtime {
namespace {

constexpr size_t maxElements = size_t{1} << 26;

std::mt19937& generator() {
    static std::mt19937 engine{std::random_device{}()};
    return engine;
}

// Arrays print as [a, b, c]; their internal id means nothing to a reader.
// Arrays, maps and structs go into a container as they are, shared like everywhere else.
// A value going into the array argument: converted to its element type when it has one.
Value stored(Call& call, const Value& value) {
    Value copy = value;
    const Object* items = call.at(0).ref();
    if (items && items->elementType) storeElement(*items, copy);
    return copy;
}

Object& mapOf(Call& call, size_t index) {
    const Value& value = call.at(index);
    if (!value.is(Value::Kind::Map)) throw std::runtime_error("Type Error: " + call.what(index) + " must be a map, got '" + value.typeName() + "'");
    return *value.ref();
}

std::string keyText(Call& call, size_t index) {
    const Value& key = call.at(index);
    if (key.isString()) return key.str();
    if (key.isInt()) return std::to_string(key.asInt());
    throw std::runtime_error("Type Error: " + call.what(index) + " must be string or int, got '" + key.typeName() + "'");
}

size_t checkedIndex(Call& call, size_t argument, size_t size) {
    long long index = call.integer(argument);
    if (index < 0 || static_cast<unsigned long long>(index) >= size)
        throw std::runtime_error("Runtime Error: Array index out of bounds: " + std::to_string(index) +
                                 " (size " + std::to_string(size) + ")");
    return static_cast<size_t>(index);
}

std::string trimmed(const std::string& text) {
    auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    return text.substr(first, text.find_last_not_of(" \t\r\n") - first + 1);
}

bool parseNumber(const std::string& raw, double& out) {
    std::string text = trimmed(raw);
    if (text.empty()) return false;
    return tryNumber(Value::string(text), out);
}

bool allIntegers(Call& call) {
    return std::all_of(call.args.begin(), call.args.end(), [](const Value& v) { return v.isInt(); });
}

Value numberResult(Call& call, double result, const char* op) {
    if (allIntegers(call)) return intResult(static_cast<long long>(result), op);
    return real(result);
}

Value unaryMath(Call& call, double (*function)(double)) {
    return real(function(call.number(0)));
}

} // namespace

void addCoreBuiltins(std::vector<Builtin>& out) {
    auto add = [&](BuiltinSpec spec, Handler handler) { out.push_back({std::move(spec), handler}); };

    // Input and output
    add({"print", "void", {{"any", "values"}}, 0, true, "",
         "Печатает значения через пробел и переводит строку. Массив выводится как `[1, 2, 3]`.\n\n"
         "```skrumi\nprint(\"Счёт:\", score);\n```"},
        [](Call& c) {
            std::string line;
            for (size_t i = 0; i < c.count(); ++i) {
                if (i > 0) line += ' ';
                line += display(c.at(i));
            }
            platform::writeOutput(line + "\n");
            return nothing();
        });
    add({"input", "string", {{"string", "prompt"}}, 0, false, "",
         "Читает строку из стандартного ввода. Необязательное приглашение печатается без перевода строки. "
         "В конце ввода возвращает пустую строку.\n\n```skrumi\nstring name = input(\"Имя: \");\n```"},
        [](Call& c) {
            if (c.has(0)) platform::writeOutput(c.text(0));
            return text(platform::readInput());
        });
    add({"getch", "string", {}, 0, false, "",
         "Читает одну клавишу из терминала без ожидания Enter и без эха."},
        [](Call&) { return text(platform::getch()); });
    add({"kbhit", "bool", {}, 0, false, "",
         "Проверяет, есть ли нажатая клавиша в буфере терминала, не дожидаясь её."},
        [](Call&) { return boolean(platform::kbhit()); });
    add({"wait", "void", {{"int", "milliseconds"}}, 1, false, "",
         "Приостанавливает программу на заданное число миллисекунд.\n\n```skrumi\nwait(500);\n```"},
        [](Call& c) {
            platform::pause(static_cast<long long>(c.amount(0, 86400000)));
            return nothing();
        });
    add({"fail", "void", {{"string", "message"}}, 1, false, "",
         "Останавливает программу с ошибкой выполнения и сообщением, как любая другая ошибка: с файлом и строкой. "
         "В обработчике HTTP-запроса даёт ответ 500, а сервер продолжает работу.\n\n"
         "```skrumi\nif (price < 0) { fail(\"цена не может быть отрицательной\"); }\n```"},
        [](Call& c) -> Value { throw std::runtime_error("Runtime Error: " + c.text(0)); });
    add({"exit", "void", {{"int", "code"}}, 0, false, "",
         "Немедленно завершает программу с кодом возврата (по умолчанию `0`).\n\n```skrumi\nexit(2);\n```"},
        [](Call& c) -> Value { throw ExitRequest{c.has(0) ? static_cast<int>(c.integer(0)) : 0}; });

    // Arrays
    add({"size", "int", {{"any", "value"}}, 1, false, "",
         "Число элементов массива, пар словаря или длина строки в **байтах** UTF-8. Символы строки считает `str_length`.\n\n"
         "```skrumi\nint n = size(scores);\n```"},
        [](Call& c) {
            const Value& value = c.at(0);
            if (value.isString()) return integer(static_cast<long long>(value.str().length()));
            if (value.is(Value::Kind::Array)) return integer(static_cast<long long>(c.array(0).size()));
            if (value.is(Value::Kind::Map)) return integer(static_cast<long long>(value.ref()->items.size()));
            throw std::runtime_error("Type Error: size() requires an array, a map or a string, got '" + value.typeName() + "'");
        });
    out.back().fast = [](Value* const* args, size_t count, Value& result) {
        if (count != 1) return false;
        const Value& value = *args[0];
        size_t size;
        if (value.isString()) size = value.str().size();
        else if (value.is(Value::Kind::Array) || value.is(Value::Kind::Map)) size = value.ref()->items.size();
        else return false;
        if (size > 2147483647u) return false;
        result = Value::integer(static_cast<long long>(size));
        return true;
    };
    add({"get", "any", {{"array", "items"}, {"int", "index"}}, 2, false, "",
         "Элемент массива по индексу (с нуля). То же, что `items[index]`.\n\n```skrumi\nint first = get(scores, 0);\n```"},
        [](Call& c) {
            auto& items = c.array(0);
            return items[checkedIndex(c, 1, items.size())];
        });
    add({"set", "void", {{"array", "items"}, {"int", "index"}, {"any", "value"}}, 3, false, "",
         "Записывает значение в элемент массива. То же, что `items[index] = value;`.\n\n```skrumi\nset(scores, 0, 100);\n```"},
        [](Call& c) {
            auto& items = c.array(0);
            items[checkedIndex(c, 1, items.size())] = stored(c, c.at(2));
            return nothing();
        });
    add({"push", "void", {{"array", "items"}, {"any", "value"}}, 2, false, "",
         "Добавляет значение в конец массива.\n\n```skrumi\narray names;\npush(names, \"Лис\");\n```"},
        [](Call& c) {
            auto& items = c.array(0);
            if (items.size() >= maxElements) throw std::runtime_error("Runtime Error: array is too large");
            items.push_back(stored(c, c.at(1)));
            return nothing();
        });
    out.back().fast = [](Value* const* args, size_t count, Value& result) {
        if (count != 2 || !args[0]->is(Value::Kind::Array)) return false;
        Object& array = *args[0]->ref();
        if (array.elementType || array.items.size() >= maxElements) return false;
        array.items.push_back(*args[1]);
        result.reset();
        return true;
    };
    add({"pop", "any", {{"array", "items"}}, 1, false, "",
         "Удаляет и возвращает последний элемент массива. Пустой массив — ошибка выполнения."},
        [](Call& c) {
            auto& items = c.array(0);
            if (items.empty()) throw std::runtime_error("Runtime Error: pop() from an empty array");
            Value last = items.back();
            items.pop_back();
            return last;
        });
    out.back().fast = [](Value* const* args, size_t count, Value& result) {
        if (count != 1 || !args[0]->is(Value::Kind::Array) || args[0]->ref()->items.empty()) return false;
        auto& items = args[0]->ref()->items;
        Value last = std::move(items.back());
        items.pop_back();
        result = std::move(last);
        return true;
    };
    add({"insert", "void", {{"array", "items"}, {"int", "index"}, {"any", "value"}}, 3, false, "",
         "Вставляет значение перед элементом `index`. Индекс, равный размеру, добавляет в конец."},
        [](Call& c) {
            auto& items = c.array(0);
            size_t index = checkedIndex(c, 1, items.size() + 1);
            if (items.size() >= maxElements) throw std::runtime_error("Runtime Error: array is too large");
            items.insert(items.begin() + static_cast<std::ptrdiff_t>(index), stored(c, c.at(2)));
            return nothing();
        });
    add({"remove_at", "any", {{"array", "items"}, {"int", "index"}}, 2, false, "",
         "Удаляет элемент по индексу и возвращает его; следующие элементы сдвигаются."},
        [](Call& c) {
            auto& items = c.array(0);
            size_t index = checkedIndex(c, 1, items.size());
            Value removed = items[index];
            items.erase(items.begin() + static_cast<std::ptrdiff_t>(index));
            return removed;
        });
    add({"resize", "void", {{"array", "items"}, {"int", "size"}}, 2, false, "",
         "Меняет размер массива. Новые элементы равны `0`, лишние отбрасываются."},
        [](Call& c) {
            const Object* items = c.at(0).ref();
            size_t size = c.amount(1, maxElements);
            auto& list = c.array(0);
            if (!items || !items->elementType) {
                list.resize(size, Value::integer(0));
                return nothing();
            }
            if (size < list.size()) list.resize(size);
            // Each new element its own zero value: array<Point> gets separate points.
            while (list.size() < size) list.push_back(zeroValue(*items->elementType, c.ctx));
            return nothing();
        });

    add({"gc_collect", "int", {}, 0, false, "",
         "Сразу освобождает кольца контейнеров, которые ссылаются только друг на друга (`push(a, a);`, две "
         "структуры с полями друг на друга), и возвращает, сколько контейнеров освобождено. Обычно не нужна: "
         "сборка запускается сама по мере создания массивов, словарей и структур."},
        [](Call&) { return integer(static_cast<long long>(collectCycles())); });

    // Tests
    add({"assert", "void", {{"bool", "condition"}, {"string", "message"}}, 1, false, "",
         "Ошибка `Assertion failed`, если условие ложно; сообщение поясняет, что проверялось. "
         "Для тестов `skrumi test` и проверок посреди программы.\n\n"
         "```skrumi\nassert(size(items) > 0, \"список не пуст\");\n```"},
        [](Call& c) -> Value {
            if (c.flag(0)) return nothing();
            throw std::runtime_error("Assertion failed: " + (c.has(1) ? c.text(1) : std::string("condition is false")));
        });
    add({"assert_equal", "void", {{"any", "actual"}, {"any", "expected"}, {"string", "message"}}, 2, false, "",
         "Ошибка `Assertion failed` с обоими значениями, если `actual` не равно `expected`. Числа сравниваются по "
         "величине (`2` равно `2.0`), массивы, словари и структуры — по содержимому.\n\n"
         "```skrumi\nassert_equal(add(2, 3), 5);\n```"},
        [](Call& c) -> Value {
            const Value& actual = c.at(0);
            const Value& expected = c.at(1);
            double a = 0, b = 0;
            bool numbers = actual.isNumber() && expected.isNumber();
            bool equal = numbers ? tryNumber(actual, a) && tryNumber(expected, b) && a == b : deepEqual(actual, expected);
            if (equal) return nothing();
            auto shown = [](const Value& v) { return v.isString() ? "\"" + v.str() + "\"" : display(v); };
            throw std::runtime_error("Assertion failed: " + (c.has(2) ? c.text(2) + ": " : std::string()) + "expected " +
                                     shown(expected) + ", got " + shown(actual));
        });

    // Maps
    add({"keys", "array", {{"map", "items"}}, 1, false, "",
         "Ключи словаря в порядке добавления.\n\n```skrumi\nmap ages = {\"Ann\": 30, \"Bob\": 25};\n"
         "array names = keys(ages); // [Ann, Bob]\n```"},
        [](Call& c) {
            ValueList out;
            for (const auto& key : mapOf(c, 0).keys) out.push_back(text(key));
            return makeArray(std::move(out));
        });
    add({"values", "array", {{"map", "items"}}, 1, false, "",
         "Значения словаря в порядке добавления ключей."},
        [](Call& c) {
            ValueList out;
            for (const auto& item : mapOf(c, 0).items) out.push_back(item);
            return makeArray(std::move(out));
        });
    add({"has", "bool", {{"map", "items"}, {"any", "key"}}, 2, false, "",
         "Есть ли в словаре ключ.\n\n```skrumi\nif (has(config, \"port\")) { port = config[\"port\"]; }\n```"},
        [](Call& c) { return boolean(mapOf(c, 0).find(keyText(c, 1)) >= 0); });
    add({"remove_key", "bool", {{"map", "items"}, {"any", "key"}}, 2, false, "",
         "Удаляет ключ из словаря. `true`, если ключ был."},
        [](Call& c) {
            Object& map = mapOf(c, 0);
            if (map.frozen) throw std::runtime_error("Runtime Error: the values of an enum cannot be changed");
            return boolean(map.erase(keyText(c, 1)));
        });
    add({"get_or", "any", {{"any", "items"}, {"any", "key"}, {"any", "default"}}, 3, false, "",
         "Значение словаря по ключу или элемент массива по индексу, а если его нет — `default`.\n\n"
         "```skrumi\nint port = get_or(config, \"port\", 8080);\n```"},
        [](Call& c) {
            const Value& items = c.at(0);
            if (items.is(Value::Kind::Map)) {
                long at = items.ref()->find(keyText(c, 1));
                return at < 0 ? c.at(2) : items.ref()->items[static_cast<size_t>(at)];
            }
            if (items.is(Value::Kind::Array)) {
                long long index = c.integer(1);
                bool inside = index >= 0 && static_cast<unsigned long long>(index) < items.ref()->items.size();
                return inside ? items.ref()->items[static_cast<size_t>(index)] : c.at(2);
            }
            throw std::runtime_error("Type Error: get_or() needs a map or an array, got '" + items.typeName() + "'");
        });

    add({"copy", "any", {{"any", "value"}}, 1, false, "",
         "Полная копия массива, словаря или структуры вместе со всем, что в них вложено. Присваивание `b = a` "
         "не копирует: обе переменные смотрят на один контейнер. Числа и строки возвращаются как есть.\n\n"
         "```skrumi\narray backup = copy(items);\npush(items, 4); // backup не изменился\n```"},
        [](Call& c) { return deepCopy(c.at(0)); });

    // Types and conversions
    add({"type_of", "string", {{"any", "value"}}, 1, false, "",
         "Имя типа значения: `int`, `float`, `string`, `bool`, `array`, `map`, `null`, `func` или имя структуры/перечисления."},
        [](Call& c) { return text(c.at(0).typeName()); });
    add({"to_int", "int", {{"any", "value"}}, 1, false, "",
         "Преобразует в `int`: дробное число отбрасывает дробную часть, строка должна содержать число, "
         "`true`/`false` дают 1/0. Некорректная строка — ошибка выполнения, проверяйте её `is_number`.\n\n"
         "```skrumi\nint port = to_int(env_default(\"PORT\", \"8080\"));\n```"},
        [](Call& c) {
            const Value& value = c.at(0);
            if (value.isBool()) return integer(c.flag(0) ? 1 : 0);
            if (value.is(Value::Kind::Array)) throw std::runtime_error("Type Error: to_int() cannot convert an array");
            double number = 0;
            if (value.isString() ? !parseNumber(value.str(), number) : !tryNumber(value, number))
                throw std::runtime_error("Runtime Error: to_int() cannot convert '" + value.text() + "' to a number");
            return intValue(Value::real(std::trunc(number)), "to_int() result");
        });
    add({"to_float", "float", {{"any", "value"}}, 1, false, "",
         "Преобразует число, строку с числом или `bool` в `float`. Некорректная строка — ошибка выполнения."},
        [](Call& c) {
            const Value& value = c.at(0);
            if (value.isBool()) return real(c.flag(0) ? 1.0 : 0.0);
            if (value.is(Value::Kind::Array)) throw std::runtime_error("Type Error: to_float() cannot convert an array");
            double number = 0;
            if (value.isString() ? !parseNumber(value.str(), number) : !tryNumber(value, number))
                throw std::runtime_error("Runtime Error: to_float() cannot convert '" + value.text() + "' to a number");
            return real(number);
        });
    add({"to_string", "string", {{"any", "value"}}, 1, false, "",
         "Текстовое представление значения; массив выводится как `[1, 2, 3]`."},
        [](Call& c) { return text(display(c.at(0))); });
    add({"is_number", "bool", {{"string", "text"}}, 1, false, "",
         "Проверяет, что строка целиком — число (`42`, `-3.5`, пробелы по краям допустимы).\n\n"
         "```skrumi\nif (is_number(answer)) { int n = to_int(answer); }\n```"},
        [](Call& c) {
            double ignored = 0;
            return boolean(parseNumber(c.text(0), ignored));
        });

    // Math
    add({"round", "int", {{"number", "value"}}, 1, false, "",
         "Округляет до ближайшего целого (половина — от нуля).\n\n```skrumi\nint r = round(3.6); // 4\n```"},
        [](Call& c) { return integer(static_cast<long long>(std::round(c.number(0)))); });
    add({"random", "int", {{"int", "min"}, {"int", "max"}}, 2, false, "",
         "Случайное целое от `min` до `max` включительно.\n\n```skrumi\nint dice = random(1, 6);\n```"},
        [](Call& c) {
            long long low = c.integer(0), high = c.integer(1);
            if (low > high)
                throw std::runtime_error("Runtime Error: random() lower bound " + std::to_string(low) +
                                         " is greater than upper bound " + std::to_string(high));
            std::uniform_int_distribution<long long> distribution(low, high);
            return integer(distribution(generator()));
        });
    add({"random_float", "float", {}, 0, false, "",
         "Случайное дробное число из полуинтервала [0, 1)."},
        [](Call&) { return real(std::uniform_real_distribution<double>(0.0, 1.0)(generator())); });
    add({"abs", "number", {{"number", "value"}}, 1, false, "",
         "Модуль числа. Сохраняет тип аргумента: `int` для `int`, иначе `float`."},
        [](Call& c) { return numberResult(c, std::fabs(c.number(0)), "abs"); });
    add({"min", "number", {{"number", "a"}, {"number", "b"}}, 2, false, "",
         "Меньшее из двух чисел; `int`, если оба аргумента `int`."},
        [](Call& c) { return numberResult(c, std::min(c.number(0), c.number(1)), "min"); });
    add({"max", "number", {{"number", "a"}, {"number", "b"}}, 2, false, "",
         "Большее из двух чисел; `int`, если оба аргумента `int`."},
        [](Call& c) { return numberResult(c, std::max(c.number(0), c.number(1)), "max"); });
    add({"clamp", "number", {{"number", "value"}, {"number", "min"}, {"number", "max"}}, 3, false, "",
         "Ограничивает число диапазоном; границы переставляются, если `min > max`. `int`, если все аргументы `int`."},
        [](Call& c) {
            double low = c.number(1), high = c.number(2);
            if (low > high) std::swap(low, high);
            return numberResult(c, std::max(low, std::min(c.number(0), high)), "clamp");
        });
    add({"sqrt", "float", {{"number", "value"}}, 1, false, "",
         "Квадратный корень. Отрицательный аргумент — ошибка выполнения."},
        [](Call& c) {
            double value = c.number(0);
            if (value < 0) throw std::runtime_error("Runtime Error: sqrt() of a negative number: " + c.text(0));
            return real(std::sqrt(value));
        });
    add({"pow", "float", {{"number", "base"}, {"number", "exponent"}}, 2, false, "",
         "Возведение в степень. Результат вне диапазона `float` — ошибка выполнения."},
        [](Call& c) {
            double result = std::pow(c.number(0), c.number(1));
            if (!std::isfinite(result)) throw std::runtime_error("Runtime Error: pow() result is out of the float range");
            return real(result);
        });
    add({"sin", "float", {{"number", "radians"}}, 1, false, "", "Синус угла в радианах."},
        [](Call& c) { return unaryMath(c, [](double v) { return std::sin(v); }); });
    add({"cos", "float", {{"number", "radians"}}, 1, false, "", "Косинус угла в радианах."},
        [](Call& c) { return unaryMath(c, [](double v) { return std::cos(v); }); });
    add({"tan", "float", {{"number", "radians"}}, 1, false, "", "Тангенс угла в радианах."},
        [](Call& c) { return unaryMath(c, [](double v) { return std::tan(v); }); });
    add({"asin", "float", {{"number", "value"}}, 1, false, "", "Арксинус в радианах; аргумент от -1 до 1."},
        [](Call& c) {
            double value = c.number(0);
            if (value < -1 || value > 1) throw std::runtime_error("Runtime Error: asin() argument must be -1..1");
            return real(std::asin(value));
        });
    add({"acos", "float", {{"number", "value"}}, 1, false, "", "Арккосинус в радианах; аргумент от -1 до 1."},
        [](Call& c) {
            double value = c.number(0);
            if (value < -1 || value > 1) throw std::runtime_error("Runtime Error: acos() argument must be -1..1");
            return real(std::acos(value));
        });
    add({"atan", "float", {{"number", "value"}}, 1, false, "", "Арктангенс в радианах."},
        [](Call& c) { return unaryMath(c, [](double v) { return std::atan(v); }); });
    add({"atan2", "float", {{"number", "y"}, {"number", "x"}}, 2, false, "",
         "Угол вектора (x, y) в радианах от -π до π; учитывает четверть плоскости."},
        [](Call& c) { return real(std::atan2(c.number(0), c.number(1))); });
    add({"floor", "float", {{"number", "value"}}, 1, false, "", "Округление вниз: `floor(-2.5)` даёт `-3`."},
        [](Call& c) { return unaryMath(c, [](double v) { return std::floor(v); }); });
    add({"ceil", "float", {{"number", "value"}}, 1, false, "", "Округление вверх: `ceil(2.1)` даёт `3`."},
        [](Call& c) { return unaryMath(c, [](double v) { return std::ceil(v); }); });
    add({"exp", "float", {{"number", "value"}}, 1, false, "", "Экспонента: e в степени `value`."},
        [](Call& c) { return unaryMath(c, [](double v) { return std::exp(v); }); });
    add({"log", "float", {{"number", "value"}}, 1, false, "", "Натуральный логарифм; аргумент больше нуля."},
        [](Call& c) {
            if (c.number(0) <= 0) throw std::runtime_error("Runtime Error: log() argument must be positive");
            return unaryMath(c, [](double v) { return std::log(v); });
        });
    add({"log10", "float", {{"number", "value"}}, 1, false, "", "Десятичный логарифм; аргумент больше нуля."},
        [](Call& c) {
            if (c.number(0) <= 0) throw std::runtime_error("Runtime Error: log10() argument must be positive");
            return unaryMath(c, [](double v) { return std::log10(v); });
        });
}

} // namespace skrumi::runtime
