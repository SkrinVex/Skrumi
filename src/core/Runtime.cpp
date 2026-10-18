#include "skrumi/Runtime.h"
#include "skrumi/Bytecode.h"
#include "skrumi/AST.h"
#include "skrumi/Debug.h"
#include "skrumi/Platform.h"
#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <set>
#include <sstream>
#include <stdexcept>

namespace skrumi {
namespace runtime {

// Shortest text that reads back as the same double. std::to_string keeps only six
// decimals, so every float operation used to lose precision when its result was stored.
std::string formatNumber(double val) {
    if (!std::isfinite(val)) throw std::runtime_error("Runtime Error: float result is not a finite number");
    char buffer[40];
    // 15 significant digits is the widest precision that still prints short values short,
    // so at most three attempts are needed to find the shortest exact text.
    for (int digits = 15; digits < 17; ++digits) {
        std::snprintf(buffer, sizeof(buffer), "%.*g", digits, val);
        if (std::strtod(buffer, nullptr) == val) return buffer;
    }
    std::snprintf(buffer, sizeof(buffer), "%.17g", val);
    return buffer;
}

const std::string* internFile(const std::string& name) {
    static std::set<std::string> names;
    static std::mutex guard;
    std::lock_guard<std::mutex> lock(guard);
    return &*names.insert(name).first;
}

std::string displayPath(const std::string& identity) {
    if (identity.rfind("@", 0) == 0) return identity.substr(1);
    // Editors that turn "file:line:" into links ask for full paths.
    if (!platform::configEnv("SKRUMI_ABSOLUTE_PATHS").empty()) return identity;
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path path = platform::pathFromUtf8(identity);
    if (!path.is_absolute()) return identity;
    fs::path base = fs::current_path(ec);
    if (ec) return identity;
    auto relative = path.lexically_relative(base);
    if (relative.empty() || *relative.begin() == "..") {
        // The working directory may be spelled differently from the file's canonical
        // path: a Windows short 8.3 name, a symbolic link.
        fs::path canonicalBase = fs::weakly_canonical(base, ec);
        if (ec) return identity;
        relative = path.lexically_relative(canonicalBase);
        if (relative.empty() || *relative.begin() == "..") return identity;
    }
    return platform::pathToUtf8(relative.generic_string());
}

std::string locate(const std::string& message, const std::string& fallbackFile) {
    const StackGuard& guard = stackGuard();
    if (guard.line <= 0) return message;
    std::string file = displayPath(guard.file ? *guard.file : fallbackFile);
    return (file.empty() ? "line " : file + ":") + std::to_string(guard.line) + ": " + message;
}

namespace {
DebugHook* attachedDebugger = nullptr;
} // namespace

DebugHook* debugHook() { return attachedDebugger; }
void setDebugHook(DebugHook* hook) { attachedDebugger = hook; }

StackGuard& stackGuard() {
    static thread_local StackGuard state;
    return state;
}

namespace {
bool parseDouble(const std::string& text, double& out) {
    char* end = nullptr;
    double result = std::strtod(text.c_str(), &end);
    if (text.empty() || end != text.c_str() + text.size() || !std::isfinite(result)) return false;
    out = result;
    return true;
}

bool parseInt(const std::string& text, long long& out) {
    char* end = nullptr;
    errno = 0;
    long long result = std::strtoll(text.c_str(), &end, 10);
    if (text.empty() || end != text.c_str() + text.size() || errno == ERANGE) return false;
    out = result;
    return true;
}

bool fitsInt(long long value) { return value >= -2147483648LL && value <= 2147483647LL; }
} // namespace

bool tryNumber(const Value& value, double& out) {
    switch (value.kind()) {
        case Value::Kind::Int: out = static_cast<double>(value.asInt()); return true;
        case Value::Kind::Float: out = value.asFloat(); return true;
        case Value::Kind::String: return parseDouble(value.str(), out);
        default: return false;
    }
}

bool tryInt(const Value& value, long long& out) {
    long long result = 0;
    if (value.isInt()) result = value.asInt();
    // A float narrows through toInt, which truncates it.
    else if (!value.isString() || !parseInt(value.str(), result)) return false;
    if (!fitsInt(result)) return false;
    out = result;
    return true;
}

double toNumber(const Value& value, const std::string& what) {
    double result = 0;
    if (!tryNumber(value, result))
        throw std::runtime_error("Type Error: " + what + " is not a number: '" + value.text() + "'");
    return result;
}

Value realResult(double result) {
    if (!std::isfinite(result)) throw std::runtime_error("Runtime Error: float result is not a finite number");
    return Value::real(result);
}

int toInt(const Value& value, const std::string& what) {
    double number = toNumber(value, what);
    if (number < -2147483648.0 || number > 2147483647.0)
        throw std::runtime_error("Runtime Error: " + what + " does not fit in int: '" + value.text() +
                                 "' (int holds -2147483648..2147483647)");
    return static_cast<int>(number);
}

Value intValue(const Value& value, const std::string& what) {
    long long result = 0;
    if (tryInt(value, result)) return Value::integer(result);
    return Value::integer(toInt(value, what));
}

void intOverflow(long long result, const char* op) {
    throw std::runtime_error("Runtime Error: int overflow in '" + std::string(op) + "': result " + std::to_string(result) +
                             " is outside -2147483648..2147483647");
}

Value parseScalar(const std::string& type, const std::string& text, const std::string& what) {
    if (type == "string") return Value::string(text);
    if (type == "bool") {
        if (text == "true" || text == "false") return Value::boolean(text == "true");
        throw std::runtime_error("Type Error: " + what + " must be true or false, got '" + text + "'");
    }
    if (type == "int") return intValue(Value::string(text), what);
    if (type == "float") return Value::real(toNumber(Value::string(text), what));
    throw std::runtime_error("Type Error: " + what + " has type '" + type + "', which has no text form");
}

std::string display(const Value& value) {
    if (value.isVoid()) return "null";
    const Object* container = value.ref();
    if (!container) return value.isString() ? value.str() : value.text();
    if (value.isFunction()) return "<func " + container->callee->name + ">";
    if (value.is(Value::Kind::Box)) return display(container->items[0]);
    // A container inside itself (a[0] = a) is shown as [...] instead of forever.
    thread_local std::vector<const Object*> open;
    if (std::find(open.begin(), open.end(), container) != open.end() || open.size() > 64)
        return container->kind == Object::Kind::Array ? "[...]" : "{...}";
    struct Nest {
        explicit Nest(const Object* object) { open.push_back(object); }
        ~Nest() { open.pop_back(); }
    } nest(container);
    const Object& object = *container;
    std::string out;
    if (object.kind == Object::Kind::Array) {
        out = "[";
        for (size_t i = 0; i < object.items.size(); ++i) {
            if (i > 0) out += ", ";
            out += display(object.items[i]);
        }
        return out + "]";
    }
    bool isStruct = object.kind == Object::Kind::Struct;
    if (isStruct && object.structType->isEnum) return object.items[0].str(); // Color.Red shows as Red
    out = isStruct ? object.structType->name + "{" : "{";
    for (size_t i = 0; i < object.items.size(); ++i) {
        if (i > 0) out += ", ";
        out += (isStruct ? object.structType->fields[i].name : object.keys[i]) + ": " + display(object.items[i]);
    }
    return out + "}";
}

Value zeroValue(const std::string& type, Context& ctx) {
    if (isNullable(type)) return Value();
    Value::Kind container;
    std::string element;
    if (containerType(type, container, element)) {
        Value empty = container == Value::Kind::Array ? makeArray({}) : makeMap();
        empty.ref()->elementType = internFile(element);
        return empty;
    }
    if (type == "int") return Value::integer(0);
    if (type == "float") return Value::real(0);
    if (type == "string") return Value::string("");
    if (type == "bool") return Value::boolean(false);
    if (type == "array") return makeArray({});
    if (type == "map") return makeMap();
    if (auto structType = ctx.getStruct(type)) {
        if (!structType->isEnum) return construct(*structType, {}, ctx);
        // An enum starts at its first value.
        const Value& values = ctx.getRoot()->variables[type];
        if (values.is(Value::Kind::Map) && !values.ref()->items.empty()) return values.ref()->items[0];
    }
    if (type == "func") throw std::runtime_error("Type Error: a func has no empty value; give it a function or declare it func?");
    throw std::runtime_error("Type Error: unknown type '" + type + "'");
}

namespace {
// A field's default is an expression of the program, compiled once per struct type and
// run in the global scope each time a value leaves the field out.
Value defaultOf(const StructType& type, size_t field, Context& ctx) {
    if (type.defaultCode.size() != type.defaults.size()) type.defaultCode.assign(type.defaults.size(), nullptr);
    auto& code = type.defaultCode[field];
    if (!code) code = bytecode::compileExpression(*type.defaults[field]);
    return vm::run(*code, *ctx.getRoot());
}
} // namespace

Value construct(const StructType& type, ValueList args, Context& ctx) {
    auto registered = ctx.getStruct(type.name);
    if (!registered) throw std::runtime_error("Type Error: unknown type '" + type.name + "'");
    return construct(registered, args.data(), args.size(), ctx);
}

Value construct(const std::shared_ptr<const StructType>& type, Value* args, size_t count, Context& ctx) {
    if (type->isEnum)
        throw std::runtime_error("Type Error: '" + type->name + "' is an enum: its values are " + type->name + ".<name>, not built by a call");
    const auto& fields = type->fields;
    if (count > fields.size())
        throw std::runtime_error("Runtime Error: struct '" + type->name + "' has " + std::to_string(fields.size()) +
                                 " fields, got " + std::to_string(count) + " values");
    if (type->kinds.size() != fields.size()) {
        type->kinds.clear();
        for (const auto& field : fields) type->kinds.push_back(declaredKind(field.type));
    }
    Value result = Value::container(Value::Kind::Struct);
    Object* object = result.ref();
    object->structType = type;
    object->items.reserve(fields.size());
    for (size_t i = 0; i < fields.size(); ++i) {
        const auto& field = fields[i];
        Value value;
        if (i < count) value = std::move(args[i]);
        else if (i < type->defaults.size() && type->defaults[i]) value = defaultOf(*type, i, ctx);
        else value = zeroValue(field.type, ctx);
        if (!storesAsIs(type->kinds[i], value))
            coerce(field.type, value, "field '" + field.name + "' of '" + type->name + "'");
        object->items.push_back(std::move(value));
    }
    return result;
}

bool containerType(const std::string& type, Value::Kind& kind, std::string& element) {
    if (type.size() < 7 || type.back() != '>') return false;
    if (type.compare(0, 6, "array<") == 0) {
        kind = Value::Kind::Array;
        element = type.substr(6, type.size() - 7);
        return true;
    }
    if (type.compare(0, 11, "map<string,") == 0) {
        kind = Value::Kind::Map;
        element = type.substr(11, type.size() - 12);
        return true;
    }
    return false;
}

std::string typeText(const Value& value) {
    const Object* object = value.ref();
    if (!object || !object->elementType) return value.typeName();
    return object->kind == Object::Kind::Array ? "array<" + *object->elementType + ">" : "map<string," + *object->elementType + ">";
}

void storeElement(const Object& container, Value& value) {
    coerce(*container.elementType, value,
           std::string(container.kind == Object::Kind::Array ? "an element of array<" : "a value of map<string,") +
               *container.elementType + ">");
}

Value::Kind declaredKind(const std::string& type) {
    if (isNullable(type)) return declaredKind(type.substr(0, type.size() - 1));
    // array<int> and map<string,int> always take the slow path, which checks the elements.
    if (type.back() == '>') return Value::Kind::Struct;
    static const std::pair<const char*, Value::Kind> kinds[] = {
        {"void", Value::Kind::Void}, {"int", Value::Kind::Int}, {"float", Value::Kind::Float},
        {"bool", Value::Kind::Bool}, {"string", Value::Kind::String}, {"array", Value::Kind::Array},
        {"map", Value::Kind::Map}, {"func", Value::Kind::Function}};
    for (const auto& entry : kinds)
        if (type == entry.first) return entry.second;
    return Value::Kind::Struct;
}

void coerce(const std::string& type, Value& value, const std::string& what) {
    // `Point p = points[i]`: a struct already of the named type, before any parsing of it.
    if (value.is(Value::Kind::Struct) && value.ref()->structType && value.ref()->structType->name == type) return;
    if (isNullable(type)) {
        if (value.isVoid()) return;
        coerce(type.substr(0, type.size() - 1), value, what);
        return;
    }
    Value::Kind container;
    std::string element;
    if (containerType(type, container, element)) {
        // The container itself takes the type: its elements are converted once, later
        // writes are checked. One already typed differently is an error.
        if (value.kind() == container) {
            Object& object = *value.ref();
            const std::string* tag = internFile(element);
            if (object.elementType == tag) return;
            if (!object.elementType) {
                for (auto& item : object.items)
                    coerce(element, item, (container == Value::Kind::Array ? "an element of " : "a value of ") + what);
                object.elementType = tag;
                return;
            }
        }
        throw std::runtime_error("Type Error: " + what + " has type '" + type + "' and cannot hold a value of type '" +
                                 typeText(value) + "'");
    }
    switch (value.kind()) {
        case Value::Kind::Int:
            if (type == "int") {
                if (!fitsInt(value.asInt())) value = intValue(value, what);
                return;
            }
            if (type == "float") {
                value = Value::real(static_cast<double>(value.asInt()));
                return;
            }
            break;
        case Value::Kind::Float:
            if (type == "float") return;
            if (type == "int") {
                throw std::runtime_error("Type Error: " + what + " cannot implicitly convert float to int; use to_int()");
            }
            break;
        case Value::Kind::String:
        case Value::Kind::Void:
            if (type == value.typeName()) return;
            break;
        default:
            if (type == value.typeName()) return;
            if (type == "string" && value.isBool()) {
                value = Value::string(value.text());
                return;
            }
            break;
    }
    if (type == "string" && value.isNumber()) {
        value = Value::string(value.text());
        return;
    }
    throw std::runtime_error("Type Error: " + what + " has type '" + type + "' and cannot hold a value of type '" +
                             typeText(value) + "'");
}

int getLogLevelThreshold() {
    std::string raw = platform::configEnv("SKRUMI_LOG_LEVEL");
    std::string level = !raw.empty() ? raw : "info";
    std::transform(level.begin(), level.end(), level.begin(), [](unsigned char ch){ return static_cast<char>(std::tolower(ch)); });
    if (level == "debug") return 10;
    if (level == "info") return 20;
    if (level == "warn" || level == "warning") return 30;
    if (level == "error") return 40;
    if (level == "off" || level == "none") return 99;
    return 20;
}

static int hexVal(char ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}

static void appendUtf8(std::string& out, unsigned cp) {
    if (cp <= 0x7F) {
        out += static_cast<char>(cp);
    } else if (cp <= 0x7FF) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp <= 0xFFFF) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

Value jsonEscape(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    appendJsonEscaped(out, text);
    return Value::string(std::move(out));
}

void appendJsonEscaped(std::string& out, const std::string& text) {
    for (char ch : text) {
        switch (ch) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            default:
                if (static_cast<unsigned char>(ch) < 0x20) {
                    char code[8];
                    std::snprintf(code, sizeof(code), "\\u%04x", static_cast<unsigned>(static_cast<unsigned char>(ch)));
                    out += code;
                } else {
                    out += ch;
                }
        }
    }
}

namespace {

// Reads a JSON document once, building the values as it goes.
class JsonReader {
public:
    JsonReader(const std::string& text, const char* what) : p_(text.data()), end_(text.data() + text.size()), what_(what) {}

    Value document() {
        Value result = value(0);
        whitespace();
        if (p_ != end_) invalid();
        return result;
    }

private:
    const char* p_;
    const char* end_;
    const char* what_;

    [[noreturn]] void invalid() const {
        throw std::runtime_error(std::string("Runtime Error: ") + what_ + " got text that is not valid JSON");
    }
    static bool space(char ch) {
        return ch == ' ' || ch == '\n' || ch == '\t' || ch == '\r' || ch == '\v' || ch == '\f'; // isspace() in the C locale
    }
    void whitespace() {
        while (p_ < end_ && space(*p_)) ++p_;
    }
    bool take(char ch) {
        whitespace();
        if (p_ < end_ && *p_ == ch) {
            ++p_;
            return true;
        }
        return false;
    }

    Value value(int depth) {
        if (depth > 64) throw std::runtime_error(std::string("Runtime Error: ") + what_ + " nesting is too deep");
        whitespace();
        if (p_ >= end_) invalid();
        switch (*p_) {
            case '{': {
                ++p_;
                Value map = makeMap();
                if (take('}')) return map;
                do {
                    whitespace();
                    if (p_ >= end_ || *p_ != '"') invalid();
                    std::string key = string();
                    if (!take(':')) invalid();
                    Value item = value(depth + 1);
                    map.ref()->slot(std::move(key)) = std::move(item);
                } while (take(','));
                if (!take('}')) invalid();
                return map;
            }
            case '[': {
                ++p_;
                Value array = makeArray({});
                if (take(']')) return array;
                auto& items = array.ref()->items;
                do items.push_back(value(depth + 1));
                while (take(','));
                if (!take(']')) invalid();
                return array;
            }
            case '"':
                return Value::string(string());
            default:
                return scalar();
        }
    }

    std::string string() {
        ++p_; // the opening quote
        std::string out;
        while (true) {
            const char* start = p_;
            while (p_ < end_ && *p_ != '"' && *p_ != '\\') ++p_;
            out.append(start, p_);
            if (p_ >= end_) invalid();
            if (*p_++ == '"') return out;
            if (p_ >= end_) invalid();
            char ch = *p_++;
            switch (ch) {
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'u': {
                    unsigned cp = hex();
                    // A high surrogate followed by a low one encodes a single code point (emoji).
                    if (cp >= 0xD800 && cp <= 0xDBFF && end_ - p_ >= 6 && p_[0] == '\\' && p_[1] == 'u') {
                        const char* mark = p_;
                        p_ += 2;
                        unsigned low = 0;
                        bool valid = true;
                        for (int i = 0; i < 4; ++i) {
                            int digit = hexVal(p_[i]);
                            if (digit < 0) valid = false;
                            low = (low << 4) | static_cast<unsigned>(digit < 0 ? 0 : digit);
                        }
                        if (valid && low >= 0xDC00 && low <= 0xDFFF) {
                            p_ += 4;
                            cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                        } else {
                            p_ = mark;
                        }
                    }
                    appendUtf8(out, cp);
                    break;
                }
                default: out += ch; // \" \\ \/ and anything else stand for themselves
            }
        }
    }

    unsigned hex() {
        if (end_ - p_ < 4) invalid();
        unsigned cp = 0;
        for (int i = 0; i < 4; ++i) {
            int digit = hexVal(p_[i]);
            if (digit < 0) invalid();
            cp = (cp << 4) | static_cast<unsigned>(digit);
        }
        p_ += 4;
        return cp;
    }

    Value scalar() {
        const char* start = p_;
        while (p_ < end_ && *p_ != ',' && *p_ != '}' && *p_ != ']' && !space(*p_)) ++p_;
        // A plain int, the usual number in JSON, is read in place.
        const char* digit = start + (p_ - start > 1 && *start == '-' ? 1 : 0);
        if (p_ > digit && p_ - digit <= 9) {
            long long whole = 0;
            const char* at = digit;
            for (; at < p_ && *at >= '0' && *at <= '9'; ++at) whole = whole * 10 + (*at - '0');
            if (at == p_) return Value::integer(digit == start ? whole : -whole);
        }
        std::string token(start, p_);
        if (token == "true") return Value::boolean(true);
        if (token == "false") return Value::boolean(false);
        if (token == "null") return Value();
        if (token.empty()) invalid();
        char* stop = nullptr;
        if (token.find_first_of(".eE") == std::string::npos) {
            errno = 0;
            long long whole = std::strtoll(token.c_str(), &stop, 10);
            if (*stop) invalid();
            // Beyond int's range a whole number is kept as a float, as it always was.
            if (errno != ERANGE && whole >= -2147483648LL && whole <= 2147483647LL) return Value::integer(whole);
        }
        double number = std::strtod(token.c_str(), &stop);
        if (*stop) invalid();
        return realResult(number);
    }
};

} // namespace

Value parseJson(const std::string& text, const char* what) {
    return JsonReader(text, what).document();
}

namespace {

// Walks a JSON document without building a tree. Every method leaves pos just past
// what it consumed and reports malformed input by returning false.
class JsonCursor {
public:
    explicit JsonCursor(const std::string& text) : json(text) {}
    const std::string& json;
    size_t pos = 0;

    void whitespace() {
        while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos]))) ++pos;
    }
    bool at(char ch) {
        whitespace();
        return pos < json.size() && json[pos] == ch;
    }

    bool string(std::string* out) {
        if (!at('"')) return false;
        ++pos;
        while (pos < json.size()) {
            char ch = json[pos++];
            if (ch == '"') return true;
            if (ch != '\\') {
                if (out) *out += ch;
                continue;
            }
            if (pos >= json.size()) return false;
            ch = json[pos++];
            if (!out) {
                if (ch == 'u') pos += 4;
                continue;
            }
            switch (ch) {
                case 'n': *out += '\n'; break;
                case 'r': *out += '\r'; break;
                case 't': *out += '\t'; break;
                case 'b': *out += '\b'; break;
                case 'f': *out += '\f'; break;
                case 'u': {
                    unsigned cp = 0;
                    if (!hex(cp)) return false;
                    // A high surrogate followed by a low one encodes a single code point (emoji).
                    if (cp >= 0xD800 && cp <= 0xDBFF && pos + 1 < json.size() && json[pos] == '\\' && json[pos + 1] == 'u') {
                        size_t mark = pos;
                        pos += 2;
                        unsigned low = 0;
                        if (hex(low) && low >= 0xDC00 && low <= 0xDFFF) cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                        else pos = mark;
                    }
                    appendUtf8(*out, cp);
                    break;
                }
                default: *out += ch;
            }
        }
        return false;
    }

    bool value() {
        whitespace();
        if (pos >= json.size()) return false;
        char ch = json[pos];
        if (ch == '"') return string(nullptr);
        if (ch == '{' || ch == '[') {
            char close = ch == '{' ? '}' : ']';
            ++pos;
            if (at(close)) { ++pos; return true; }
            while (true) {
                if (close == '}') {
                    if (!string(nullptr) || !at(':')) return false;
                    ++pos;
                }
                if (!value()) return false;
                if (at(',')) { ++pos; continue; }
                if (at(close)) { ++pos; return true; }
                return false;
            }
        }
        size_t start = pos;
        while (pos < json.size() && json[pos] != ',' && json[pos] != '}' && json[pos] != ']' &&
               !std::isspace(static_cast<unsigned char>(json[pos]))) ++pos;
        return pos > start;
    }

    // Moves to the value at the path; false when the path does not exist.
    bool locate(const std::string& path) {
        std::string normalized = path;
        for (auto& ch : normalized) if (ch == '[') ch = '.';
        normalized.erase(std::remove(normalized.begin(), normalized.end(), ']'), normalized.end());
        std::stringstream segments(normalized);
        std::string segment;
        while (std::getline(segments, segment, '.')) {
            if (segment.empty()) continue;
            if (at('{')) {
                ++pos;
                bool found = false;
                while (!at('}')) {
                    std::string key;
                    if (!string(&key) || !at(':')) return false;
                    ++pos;
                    if (key == segment) { found = true; break; }
                    if (!value()) return false;
                    if (at(',')) ++pos;
                    else if (!at('}')) return false;
                }
                if (!found) return false;
            } else if (at('[')) {
                if (segment.find_first_not_of("0123456789") != std::string::npos || segment.size() > 9) return false;
                size_t index = std::stoul(segment);
                ++pos;
                for (size_t i = 0; i < index; ++i) {
                    if (at(']') || !value()) return false;
                    if (!at(',')) return false;
                    ++pos;
                }
                if (at(']')) return false;
            } else {
                return false;
            }
        }
        whitespace();
        return pos < json.size();
    }

    int count() {
        whitespace();
        if (pos >= json.size() || (json[pos] != '{' && json[pos] != '[')) return -1;
        char close = json[pos] == '{' ? '}' : ']';
        ++pos;
        int items = 0;
        if (at(close)) return 0;
        while (true) {
            if (close == '}') {
                if (!string(nullptr) || !at(':')) return -1;
                ++pos;
            }
            if (!value()) return -1;
            ++items;
            if (at(',')) { ++pos; continue; }
            return at(close) ? items : -1;
        }
    }

private:
    bool hex(unsigned& cp) {
        if (pos + 4 > json.size()) return false;
        for (int i = 0; i < 4; ++i) {
            int digit = hexVal(json[pos + static_cast<size_t>(i)]);
            if (digit < 0) return false;
            cp = (cp << 4) | static_cast<unsigned>(digit);
        }
        pos += 4;
        return true;
    }
};

} // namespace

bool jsonRaw(const std::string& json, const std::string& path, std::string& out) {
    JsonCursor cursor(json);
    if (!cursor.locate(path)) return false;
    size_t start = cursor.pos;
    if (!cursor.value()) return false;
    out = json.substr(start, cursor.pos - start);
    return true;
}

std::vector<std::pair<std::string, std::string>> jsonEntries(const std::string& json) {
    std::vector<std::pair<std::string, std::string>> entries;
    JsonCursor cursor(json);
    cursor.whitespace();
    if (cursor.pos >= json.size() || (json[cursor.pos] != '{' && json[cursor.pos] != '[')) return entries;
    bool object = json[cursor.pos] == '{';
    char close = object ? '}' : ']';
    ++cursor.pos;
    while (!cursor.at(close)) {
        std::string key;
        if (object) {
            if (!cursor.string(&key) || !cursor.at(':')) return {};
            ++cursor.pos;
        }
        cursor.whitespace();
        size_t start = cursor.pos;
        if (!cursor.value()) return {};
        entries.push_back({key, json.substr(start, cursor.pos - start)});
        if (cursor.at(',')) ++cursor.pos;
        else if (!cursor.at(close)) return {};
    }
    return entries;
}

bool jsonValid(const std::string& json) {
    JsonCursor cursor(json);
    if (!cursor.value()) return false;
    cursor.whitespace();
    return cursor.pos == json.size();
}

namespace {

std::vector<std::string> pathSegments(const std::string& path) {
    std::string normalized = path;
    for (auto& ch : normalized) if (ch == '[') ch = '.';
    normalized.erase(std::remove(normalized.begin(), normalized.end(), ']'), normalized.end());
    std::vector<std::string> segments;
    std::stringstream stream(normalized);
    for (std::string segment; std::getline(stream, segment, '.');)
        if (!segment.empty()) segments.push_back(segment);
    return segments;
}

std::string setAt(const std::string& doc, const std::vector<std::string>& segments, size_t index,
                  const std::string& raw, const std::string& path) {
    if (index == segments.size()) return raw;
    const std::string& segment = segments[index];
    std::string text = doc;
    auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) text = "{}";
    JsonCursor cursor(text);
    cursor.whitespace();
    char open = text[cursor.pos];
    if (open == '{') {
        ++cursor.pos;
        bool any = false;
        while (!cursor.at('}')) {
            std::string key;
            if (!cursor.string(&key) || !cursor.at(':')) break;
            ++cursor.pos;
            cursor.whitespace();
            size_t start = cursor.pos;
            if (!cursor.value()) break;
            any = true;
            if (key == segment) {
                return text.substr(0, start) + setAt(text.substr(start, cursor.pos - start), segments, index + 1, raw, path) +
                       text.substr(cursor.pos);
            }
            if (cursor.at(',')) ++cursor.pos;
        }
        if (!cursor.at('}')) throw std::runtime_error("Runtime Error: json_set() got malformed JSON");
        std::string member = jsonEscape(segment).str();
        return text.substr(0, cursor.pos) + (any ? "," : "") + "\"" + member + "\":" +
               setAt("", segments, index + 1, raw, path) + text.substr(cursor.pos);
    }
    if (open == '[' && segment.find_first_not_of("0123456789") == std::string::npos && segment.size() < 10) {
        size_t wanted = std::stoul(segment), position = 0;
        ++cursor.pos;
        while (!cursor.at(']')) {
            cursor.whitespace();
            size_t start = cursor.pos;
            if (!cursor.value()) throw std::runtime_error("Runtime Error: json_set() got malformed JSON");
            if (position++ == wanted) {
                return text.substr(0, start) + setAt(text.substr(start, cursor.pos - start), segments, index + 1, raw, path) +
                       text.substr(cursor.pos);
            }
            if (cursor.at(',')) ++cursor.pos;
        }
        if (wanted != position)
            throw std::runtime_error("Runtime Error: json_set() index " + segment + " is past the end of the array in '" + path + "'");
        return text.substr(0, cursor.pos) + (position ? "," : "") + setAt("", segments, index + 1, raw, path) + text.substr(cursor.pos);
    }
    throw std::runtime_error("Runtime Error: json_set() cannot set '" + path + "': '" + segment + "' is not inside an object");
}

} // namespace

std::string jsonSet(const std::string& json, const std::string& path, const std::string& raw) {
    return setAt(json, pathSegments(path), 0, raw, path);
}

Value jsonGet(const std::string& json, const std::string& path) {
    JsonCursor cursor(json);
    if (!cursor.locate(path)) return Value::string("");
    if (json[cursor.pos] == '"') {
        std::string decoded;
        if (!cursor.string(&decoded)) return Value::string("");
        return Value::string(std::move(decoded));
    }
    size_t start = cursor.pos;
    if (!cursor.value()) return Value::string("");
    return Value::string(json.substr(start, cursor.pos - start));
}

int jsonCount(const std::string& json, const std::string& path) {
    JsonCursor cursor(json);
    if (!cursor.locate(path)) return -1;
    return cursor.count();
}

std::string jsonType(const std::string& json, const std::string& path) {
    JsonCursor cursor(json);
    if (!cursor.locate(path)) return "";
    switch (json[cursor.pos]) {
        case '{': return "object";
        case '[': return "array";
        case '"': return "string";
        case 't': case 'f': return "bool";
        case 'n': return "null";
        default: return "number";
    }
}

void loadDotEnv(const std::string& scriptPath) {
    std::string dir = ".";
    size_t slash = scriptPath.find_last_of("/\\");
    if (slash != std::string::npos) dir = scriptPath.substr(0, slash);

    std::ifstream env(platform::pathFromUtf8(dir) / ".env");
    if (!env.is_open()) env.open(".env");
    if (!env.is_open()) return;

    std::string line;
    while (std::getline(env, line)) {
        if (line.empty() || line[0] == '#') continue;
        if (line.rfind("export ", 0) == 0) line = line.substr(7);
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;

        std::string key = line.substr(0, eq);
        std::string value = line.substr(eq + 1);

        while (!key.empty() && (key.back() == ' ' || key.back() == '\t')) key.pop_back();
        size_t ks = key.find_first_not_of(" \t");
        if (ks != std::string::npos) key = key.substr(ks);

        if (value.size() >= 2 && ((value.front() == '"' && value.back() == '"') || (value.front() == '\'' && value.back() == '\''))) {
            value = value.substr(1, value.size() - 2);
        }

        if (!key.empty() && platform::getEnvVar(key).empty()) {
            platform::setEnvVar(key, value);
        }
    }
}

std::string resolveSourceFile(const std::string& requested, const std::string& currentFile, const std::string& skrumiHome) {
    namespace fs = std::filesystem;
    std::vector<fs::path> candidates;
    fs::path req = fs::u8path(requested);

    if (req.is_absolute()) candidates.push_back(req);
    if (!currentFile.empty()) {
        size_t found = currentFile.find_last_of("/\\");
        std::string dir = (found == std::string::npos) ? "./" : currentFile.substr(0, found + 1);
        candidates.push_back(platform::pathFromUtf8(dir) / req);
    }
    candidates.push_back(platform::pathFromUtf8(requested));

    std::string home = skrumiHome.empty() ? platform::configEnv("SKRUMI_HOME") : skrumiHome;
    if (!home.empty()) {
        candidates.push_back(platform::pathFromUtf8(home) / req);
        candidates.push_back(platform::pathFromUtf8(home) / "std" / req);
    }

    for (const auto& candidate : candidates) {
        std::error_code ec;
        if (fs::exists(candidate, ec) && fs::is_regular_file(candidate, ec)) {
            return platform::pathToUtf8(fs::weakly_canonical(candidate, ec));
        }
    }
    throw std::runtime_error("Module Error: File '" + requested + "' not found. Checked current file, working directory and SKRUMI_HOME.");
}

} // namespace runtime
} // namespace skrumi
