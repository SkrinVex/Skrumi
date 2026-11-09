// String and JSON builtins. Positions and lengths count characters (code points),
// never UTF-8 bytes, so Cyrillic and emoji behave like Latin text.
#include "Builtin.h"
#include "skrumi/Unicode.h"
#include "../Operations.h"
#include "../HttpServer.h"
#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace skrumi::runtime {
namespace {

size_t sequenceLength(unsigned char lead) {
    if (lead < 0x80) return 1;
    if ((lead & 0xE0) == 0xC0) return 2;
    if ((lead & 0xF0) == 0xE0) return 3;
    if ((lead & 0xF8) == 0xF0) return 4;
    return 1;
}

std::vector<std::string> characters(const std::string& text) {
    std::vector<std::string> out;
    for (size_t i = 0; i < text.size();) {
        size_t length = std::min(sequenceLength(static_cast<unsigned char>(text[i])), text.size() - i);
        out.push_back(text.substr(i, length));
        i += length;
    }
    return out;
}

size_t characterCount(const std::string& text) {
    // Every byte but a continuation byte (10xxxxxx) starts a character; eight bytes
    // are looked at together.
    const char* data = text.data();
    size_t size = text.size(), i = 0, continuations = 0;
    for (; i + 8 <= size; i += 8) {
        std::uint64_t word;
        std::memcpy(&word, data + i, 8);
        std::uint64_t marks = (word & ~(word << 1) & 0x8080808080808080ULL) >> 7;
        continuations += static_cast<size_t>((marks * 0x0101010101010101ULL) >> 56);
    }
    for (; i < size; ++i)
        if ((static_cast<unsigned char>(data[i]) & 0xC0) == 0x80) ++continuations;
    return size - continuations;
}

// Byte offset of the character with the given index, or text.size() past the end.
size_t byteOffset(const std::string& text, size_t character) {
    size_t seen = 0;
    for (size_t i = 0; i < text.size(); ++i) {
        if ((static_cast<unsigned char>(text[i]) & 0xC0) == 0x80) continue;
        if (seen == character) return i;
        ++seen;
    }
    return text.size();
}

unsigned decode(const std::string& c) {
    auto byte = [&](size_t i) { return static_cast<unsigned>(static_cast<unsigned char>(c[i])); };
    switch (c.size()) {
        case 2: return ((byte(0) & 0x1F) << 6) | (byte(1) & 0x3F);
        case 3: return ((byte(0) & 0x0F) << 12) | ((byte(1) & 0x3F) << 6) | (byte(2) & 0x3F);
        case 4: return ((byte(0) & 0x07) << 18) | ((byte(1) & 0x3F) << 12) | ((byte(2) & 0x3F) << 6) | (byte(3) & 0x3F);
        default: return byte(0);
    }
}

std::string encode(unsigned cp) {
    std::string out;
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
    return out;
}

// Latin and Cyrillic letters, the alphabets Skrumi programs are written in.
std::string changeCase(const std::string& text, bool upper) {
    std::string out;
    out.reserve(text.size());
    for (const auto& c : characters(text)) {
        unsigned cp = decode(c);
        unsigned mapped = cp;
        if (upper) {
            if (cp >= 'a' && cp <= 'z') mapped = cp - 32;
            else if (cp >= 0x430 && cp <= 0x44F) mapped = cp - 0x20;
            else if (cp >= 0x450 && cp <= 0x45F) mapped = cp - 0x50;
        } else {
            if (cp >= 'A' && cp <= 'Z') mapped = cp + 32;
            else if (cp >= 0x410 && cp <= 0x42F) mapped = cp + 0x20;
            else if (cp >= 0x400 && cp <= 0x40F) mapped = cp + 0x50;
        }
        out += mapped == cp ? c : encode(mapped);
    }
    return out;
}

std::string scalarText(const Value& value, const std::string& what) {
    if (value.is(Value::Kind::Array)) throw std::runtime_error("Type Error: " + what + " cannot contain nested arrays");
    return value.isString() ? value.str() : value.text();
}

constexpr size_t maxText = size_t{64} * 1024 * 1024;

// A Skrumi value as JSON, appended to out: numbers and bools as literals, strings
// quoted, arrays as JSON arrays, maps and structs as objects.
void appendJson(std::string& out, const Value& value, int depth = 0) {
    if (value.isString()) {
        out += '"';
        appendJsonEscaped(out, value.str());
        out += '"';
        return;
    }
    if (value.isInt()) {
        appendDisplay(out, value);
        return;
    }
    if (value.isNumber() || value.isBool()) {
        out += value.text();
        return;
    }
    if (value.isVoid()) {
        out += "null";
        return;
    }
    if (value.is(Value::Kind::Struct) && value.ref()->structType->isEnum) {
        appendJson(out, value.ref()->items[0], depth);
        return;
    }
    if (!value.ref() || value.isFunction())
        throw std::runtime_error("Type Error: json_value() cannot convert '" + value.typeName() + "'");
    if (depth > 64) throw std::runtime_error("Runtime Error: json_value() nesting is too deep");
    const Object& object = *value.ref();
    bool isArray = object.kind == Object::Kind::Array;
    out += isArray ? '[' : '{';
    for (size_t i = 0; i < object.items.size(); ++i) {
        if (i > 0) out += ',';
        if (!isArray) {
            const std::string& key = object.kind == Object::Kind::Map ? object.keys[i] : object.structType->fields[i].name;
            out += '"';
            appendJsonEscaped(out, key);
            out += "\":";
        }
        appendJson(out, object.items[i], depth + 1);
    }
    out += isArray ? ']' : '}';
}

std::string toJson(const Value& value) {
    std::string out;
    appendJson(out, value);
    return out;
}

} // namespace

void addTextBuiltins(std::vector<Builtin>& out) {
    auto add = [&](BuiltinSpec spec, Handler handler) { out.push_back({std::move(spec), handler}); };

    add({"byte_size", "int", {{"string", "text"}}, 1, false, "string", "Размер строки в байтах UTF-8 (как size для строки)."},
        [](Call& c) { return integer(static_cast<long long>(c.text(0).size())); });
    add({"char_length", "int", {{"string", "text"}}, 1, false, "string", "Число кодовых точек Unicode, синоним str_length; не графемы."},
        [](Call& c) { return integer(static_cast<long long>(characterCount(c.text(0)))); });
    add({"grapheme_length", "int", {{"string", "text"}}, 1, false, "string", "Число расширенных графемных кластеров, Unicode 16 UAX #29."},
        [](Call& c) { return integer(static_cast<long long>(unicode::graphemeBoundaries(c.text(0)).size() - 1)); });
    add({"grapheme_split", "array", {{"string", "text"}}, 1, false, "string", "Разбивает строку по графемам, сохраняя ZWJ, флаги и комбинируемые знаки."},
        [](Call& c) { ValueList parts; for (auto& part : unicode::graphemes(c.text(0))) parts.push_back(text(std::move(part))); return makeArray(std::move(parts)); });
    add({"grapheme_substring", "string", {{"string", "text"}, {"int", "start"}, {"int", "length"}}, 2, false, "string", "Подстрока по графемам; неотрицательные start/length, без length — до конца, границы ограничиваются длиной."},
        [](Call& c) {
            auto boundaries = unicode::graphemeBoundaries(c.text(0));
            size_t count = boundaries.size() - 1;
            size_t start = std::min(c.amount(1, maxText), count);
            size_t end = c.has(2) ? start + std::min(c.amount(2, maxText), count - start) : count;
            return text(c.text(0).substr(boundaries[start], boundaries[end] - boundaries[start]));
        });
    add({"str_length", "int", {{"string", "text"}}, 1, false, "string",
         "Число символов строки. `size(text)` для строки считает байты UTF-8.\n\n"
         "```skrumi\nint n = str_length(\"Лисий\"); // 5, а size() даст 10\n```"},
        [](Call& c) { return integer(static_cast<long long>(characterCount(c.text(0)))); });
    out.back().fast = [](Value* const* args, size_t count, Value& result) {
        if (count != 1 || !args[0]->isString()) return false;
        long long characters = static_cast<long long>(characterCount(args[0]->str()));
        result = Value::integer(characters);
        return true;
    };
    add({"str_contains", "bool", {{"string", "text"}, {"string", "needle"}}, 2, false, "string",
         "Содержит ли `text` подстроку `needle`."},
        [](Call& c) { return boolean(c.text(0).find(c.text(1)) != std::string::npos); });
    add({"str_replace", "string", {{"string", "text"}, {"string", "from"}, {"string", "to"}}, 3, false, "string",
         "Заменяет все вхождения `from` на `to`. Пустой `from` оставляет строку без изменений."},
        [](Call& c) {
            std::string result = c.text(0);
            const std::string& from = c.text(1);
            const std::string& to = c.text(2);
            if (from.empty()) return text(result);
            for (size_t pos = 0; (pos = result.find(from, pos)) != std::string::npos; pos += to.size()) {
                result.replace(pos, from.size(), to);
                if (result.size() > maxText) throw std::runtime_error("Runtime Error: str_replace() result is too large");
            }
            return text(result);
        });
    add({"str_split", "array", {{"string", "text"}, {"string", "delimiter"}}, 2, false, "string",
         "Разбивает строку по разделителю и возвращает массив строк. Пустой разделитель делит на символы.\n\n"
         "```skrumi\narray parts = str_split(\"a,b,c\", \",\");\n```"},
        [](Call& c) {
            ValueList parts;
            const std::string& source = c.text(0);
            const std::string& delimiter = c.text(1);
            if (delimiter.empty()) {
                for (auto& character : characters(source)) parts.push_back(text(character));
            } else {
                const char* at = source.data();
                const char* end = at + source.size();
                if (delimiter.size() == 1) {
                    // The usual "," or "\n": memchr finds it fastest.
                    for (const void* found; (found = std::memchr(at, delimiter[0], static_cast<size_t>(end - at)));) {
                        const char* stop = static_cast<const char*>(found);
                        parts.push_back(Value::string(std::string(at, stop)));
                        at = stop + 1;
                    }
                } else {
                    for (size_t pos; (pos = source.find(delimiter, static_cast<size_t>(at - source.data()))) != std::string::npos;) {
                        const char* stop = source.data() + pos;
                        parts.push_back(Value::string(std::string(at, stop)));
                        at = stop + delimiter.size();
                    }
                }
                parts.push_back(Value::string(std::string(at, end)));
            }
            return makeArray(std::move(parts));
        });
    add({"str_join", "string", {{"array", "items"}, {"string", "separator"}}, 2, false, "string",
         "Склеивает элементы массива в строку через разделитель.\n\n"
         "```skrumi\nstring csv = str_join(parts, \";\");\n```"},
        [](Call& c) {
            const auto& items = c.array(0);
            const std::string& separator = c.text(1);
            // The size of the text items and the separators first: one allocation.
            size_t known = items.empty() ? 0 : separator.size() * (items.size() - 1);
            for (const Value& item : items)
                if (item.isString()) known += item.str().size();
            if (known > maxText) throw std::runtime_error("Runtime Error: str_join() result is too large");
            std::string result;
            result.reserve(known);
            for (size_t i = 0; i < items.size(); ++i) {
                if (i > 0) result += separator;
                if (items[i].isString()) result += items[i].str();
                else result += scalarText(items[i], "str_join() items");
                if (result.size() > maxText) throw std::runtime_error("Runtime Error: str_join() result is too large");
            }
            return text(std::move(result));
        });
    add({"str_upper", "string", {{"string", "text"}}, 1, false, "string",
         "Строка прописными буквами (латиница и кириллица)."},
        [](Call& c) { return text(changeCase(c.text(0), true)); });
    add({"str_lower", "string", {{"string", "text"}}, 1, false, "string",
         "Строка строчными буквами (латиница и кириллица)."},
        [](Call& c) { return text(changeCase(c.text(0), false)); });
    add({"str_trim", "string", {{"string", "text"}}, 1, false, "string",
         "Убирает пробелы, табуляции и переводы строк по краям."},
        [](Call& c) {
            const std::string& source = c.text(0);
            auto first = source.find_first_not_of(" \t\r\n");
            if (first == std::string::npos) return text("");
            return text(source.substr(first, source.find_last_not_of(" \t\r\n") - first + 1));
        });
    add({"str_starts_with", "bool", {{"string", "text"}, {"string", "prefix"}}, 2, false, "string",
         "Начинается ли строка с `prefix`."},
        [](Call& c) { return boolean(c.text(0).compare(0, c.text(1).size(), c.text(1)) == 0); });
    add({"str_ends_with", "bool", {{"string", "text"}, {"string", "suffix"}}, 2, false, "string",
         "Заканчивается ли строка на `suffix`."},
        [](Call& c) {
            const std::string& source = c.text(0);
            const std::string& suffix = c.text(1);
            return boolean(source.size() >= suffix.size() &&
                           source.compare(source.size() - suffix.size(), suffix.size(), suffix) == 0);
        });
    add({"str_index_of", "int", {{"string", "text"}, {"string", "needle"}}, 2, false, "string",
         "Номер символа, с которого начинается первое вхождение `needle`, или `-1`."},
        [](Call& c) {
            size_t found = c.text(0).find(c.text(1));
            if (found == std::string::npos) return integer(-1);
            return integer(static_cast<long long>(characterCount(c.text(0).substr(0, found))));
        });
    add({"str_substring", "string", {{"string", "text"}, {"int", "start"}, {"int", "length"}}, 2, false, "string",
         "Подстрока из `length` символов начиная с символа `start`; без `length` — до конца строки. "
         "Выход за границы строки укорачивает результат.\n\n```skrumi\nstring word = str_substring(\"Привет, мир\", 8, 3); // \"мир\"\n```"},
        [](Call& c) {
            const std::string& source = c.text(0);
            size_t start = c.amount(1, maxText);
            size_t from = byteOffset(source, start);
            if (!c.has(2)) return text(source.substr(from));
            size_t to = byteOffset(source, start + c.amount(2, maxText));
            return text(source.substr(from, to - from));
        });
    add({"str_repeat", "string", {{"string", "text"}, {"int", "count"}}, 2, false, "string",
         "Повторяет строку `count` раз."},
        [](Call& c) {
            const std::string& unit = c.text(0);
            size_t count = c.amount(1, maxText);
            if (!unit.empty() && count > maxText / unit.size())
                throw std::runtime_error("Runtime Error: str_repeat() result is too large");
            std::string result;
            result.reserve(unit.size() * count);
            for (size_t i = 0; i < count; ++i) result += unit;
            return text(result);
        });

    add({"json_get", "string", {{"string", "json"}, {"string", "path"}}, 2, false, "json",
         "Значение по точечному пути: ключи объектов и индексы массивов (`message.chat.id`, `items.0.name`). "
         "Строки декодируются (включая `\\uXXXX` и emoji), объекты и массивы возвращаются как JSON-текст. "
         "Отсутствующий путь даёт пустую строку."},
        [](Call& c) { return jsonGet(c.text(0), c.text(1)); });
    add({"json_escape", "string", {{"string", "text"}}, 1, false, "json",
         "Экранирует кавычки, обратную косую черту и управляющие символы для вставки внутрь JSON-строки."},
        [](Call& c) { return jsonEscape(c.text(0)); });
    add({"json_count", "int", {{"string", "json"}, {"string", "path"}}, 2, false, "json",
         "Число элементов массива или ключей объекта по пути; `-1`, если там нет массива или объекта. "
         "Пустой путь означает весь документ."},
        [](Call& c) { return integer(jsonCount(c.text(0), c.text(1))); });
    add({"json_type", "string", {{"string", "json"}, {"string", "path"}}, 2, false, "json",
         "Тип значения по пути: `object`, `array`, `string`, `number`, `bool`, `null` или пустая строка, если пути нет."},
        [](Call& c) { return text(jsonType(c.text(0), c.text(1))); });
    add({"json_value", "string", {{"any", "value"}}, 1, false, "",
         "Значение Skrumi в виде JSON: числа и `bool` как есть, `null` как `null`, строка в кавычках с экранированием, "
         "массив — JSON-массив, словарь и структура — JSON-объект.\n\n```skrumi\njson_value([1, \"лис\", true]) // [1,\"лис\",true]\n```"},
        [](Call& c) { return text(toJson(c.at(0))); });
    add({"json_decode", "any", {{"string", "json"}}, 1, false, "",
         "Разбирает JSON целиком: объект становится словарём `map`, массив — массивом, целые числа — `int`, "
         "дробные — `float`, `null` — `null`. Некорректный JSON — ошибка выполнения.\n\n"
         "```skrumi\nmap user = json_decode(\"{\\\"name\\\": \\\"Лис\\\", \\\"tags\\\": [1, 2]}\");\nprint(user.name, user[\"tags\"][1]);\n```"},
        [](Call& c) {
            return parseJson(c.text(0), "json_decode()");
        });
    add({"json_set", "string", {{"string", "json"}, {"string", "path"}, {"any", "value"}}, 3, false, "",
         "Новый JSON-документ, где по пути записано значение (строка — как JSON-строка, массив — как JSON-массив). "
         "Недостающие ключи объектов создаются; пустой документ считается `{}`.\n\n"
         "```skrumi\nstring user = json_set(\"\", \"name\", \"Лис\");\nuser = json_set(user, \"stats.age\", 3); // {\"name\":\"Лис\",\"stats\":{\"age\":3}}\n```"},
        [](Call& c) { return text(jsonSet(c.text(0), c.text(1), toJson(c.at(2)))); });
    add({"json_set_raw", "string", {{"string", "json"}, {"string", "path"}, {"string", "raw_json"}}, 3, false, "",
         "Как `json_set`, но значение — уже готовый JSON (объект, массив или литерал), который вставляется без кавычек. "
         "Некорректный JSON — ошибка выполнения."},
        [](Call& c) {
            if (!jsonValid(c.text(2))) throw std::runtime_error("Runtime Error: json_set_raw() value is not valid JSON");
            return text(jsonSet(c.text(0), c.text(1), c.text(2)));
        });
    add({"json_valid", "bool", {{"string", "json"}}, 1, false, "",
         "Является ли строка корректным JSON-документом. Удобно проверять тело запроса перед разбором."},
        [](Call& c) { return boolean(jsonValid(c.text(0))); });
    add({"template_render", "string", {{"string", "template"}, {"string", "json"}}, 2, false, "",
         "Заполняет HTML-шаблон данными из JSON: `{{путь}}` — значение с экранированием HTML, `{{{путь}}}` — без "
         "экранирования, `{{#each путь}}...{{/each}}` — повтор для элементов (`{{.}}`, `{{@index}}`, `{{@key}}`), "
         "`{{#if путь}}...{{else}}...{{/if}}` — условие, `{{! ...}}` — комментарий."},
        [](Call& c) { return text(renderTemplate(c.text(0), c.text(1))); });
    add({"url_encode", "string", {{"string", "text"}}, 1, false, "",
         "Кодирует текст для URL и query-строки: всё, кроме букв латиницы, цифр и `-_.~`, превращается в `%XX`."},
        [](Call& c) { return text(http::percentEncode(c.text(0))); });
    add({"url_decode", "string", {{"string", "text"}}, 1, false, "",
         "Декодирует `%XX` и `+` (пробел) из URL или данных формы."},
        [](Call& c) { return text(http::percentDecode(c.text(0), true)); });
    add({"html_escape", "string", {{"string", "text"}}, 1, false, "",
         "Экранирует `& < > \" '` для безопасной вставки текста пользователя в HTML."},
        [](Call& c) { return text(http::htmlEscape(c.text(0))); });
}

} // namespace skrumi::runtime
