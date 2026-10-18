#include "Operations.h"
#include "skrumi/Runtime.h"
#include <charconv>
#include <cmath>
#include <stdexcept>

namespace skrumi::runtime {

namespace {

std::string containerName(const Value& value) {
    if (value.is(Value::Kind::Array)) return "an array";
    if (value.is(Value::Kind::Map)) return "a map";
    return "a struct '" + value.typeName() + "'";
}

std::string describe(const char* side, const std::string& text) {
    return std::string(side) + " operand of '" + text + "'";
}

double number(const Value& value, const char* side, const std::string& text) {
    double result = 0;
    if (tryNumber(value, result)) return result;
    return toNumber(value, describe(side, text));
}

long long integer(const Value& value, const char* side, const std::string& text) {
    long long result = 0;
    if (tryInt(value, result)) return result;
    return toInt(value, describe(side, text));
}

long long intArgument(const Value& value, const char* what) {
    if (value.isInt() && value.asInt() >= -2147483648LL && value.asInt() <= 2147483647LL) return value.asInt();
    long long result = 0;
    if (tryInt(value, result)) return result;
    if (value.isFloat()) throw std::runtime_error("Type Error: " + std::string(what) + " requires int; use to_int()");
    return toInt(value, what);
}

size_t positionIn(const Object& array, const Value& indexValue) {
    long long index = intArgument(indexValue, "array index");
    if (index < 0 || static_cast<unsigned long long>(index) >= array.items.size())
        throw std::runtime_error("Runtime Error: Array index out of bounds: " + std::to_string(index) + " (size " +
                                 std::to_string(array.items.size()) + ")");
    return static_cast<size_t>(index);
}

size_t fieldIn(const Object& object, const std::string& name) {
    const auto& fields = object.structType->fields;
    for (size_t i = 0; i < fields.size(); ++i)
        if (fields[i].name == name) return i;
    throw std::runtime_error("Runtime Error: struct '" + object.structType->name + "' has no field '" + name + "'");
}


[[noreturn]] void missingKey(const std::string& key) {
    throw std::runtime_error("Runtime Error: map has no key '" + key + "' (has(map, key) checks, get_or(map, key, default) reads safely)");
}

} // namespace

void appendDisplay(std::string& out, const Value& value) {
    switch (value.kind()) {
        case Value::Kind::String: out += value.str(); break;
        case Value::Kind::Int: {
            char digits[24];
            auto end = std::to_chars(digits, digits + sizeof digits, value.asInt()).ptr;
            out.append(digits, end);
            break;
        }
        case Value::Kind::Bool: out += value.asBool() ? "true" : "false"; break;
        case Value::Kind::Void: out += "null"; break;
        default: out += display(value); break;
    }
}

void divisionByZero() { throw std::runtime_error("Runtime Error: Division by zero"); }

Operator operatorOf(const std::string& text) {
    static const std::pair<const char*, Operator> operators[] = {
        {"&&", Operator::And}, {"||", Operator::Or}, {"+", Operator::Add}, {"+=", Operator::Add},
        {"-", Operator::Sub}, {"-=", Operator::Sub}, {"*", Operator::Mul}, {"*=", Operator::Mul},
        {"/", Operator::Div}, {"/=", Operator::Div}, {"%", Operator::Mod}, {"%=", Operator::Mod},
        {"==", Operator::Eq}, {"!=", Operator::Ne}, {"<", Operator::Lt}, {"<=", Operator::Le},
        {">", Operator::Gt}, {">=", Operator::Ge}};
    for (const auto& entry : operators)
        if (text == entry.first) return entry.second;
    return Operator::Unknown;
}

bool operandTruth(const Value& value, const char* side, const std::string& text) {
    if (!value.isBool())
        throw std::runtime_error("Type Error: " + describe(side, text) + " must be bool, got '" + value.typeName() + "'");
    return value.asBool();
}

Value binary(Operator op, const std::string& text, const Value& lval, const Value& rval) {
    // Two ints are the common case: arithmetic and comparison without any conversion.
    Value result;
    if (lval.isInt() && rval.isInt() && binaryInts(op, lval.asInt(), rval.asInt(), text, result)) return result;

    bool equality = op == Operator::Eq || op == Operator::Ne;
    // null equals only null; any other comparison with it is an error below.
    if (equality && (lval.isVoid() || rval.isVoid())) return Value::boolean((lval.isVoid() && rval.isVoid()) == (op == Operator::Eq));
    bool concatenation = op == Operator::Add && (lval.isString() || rval.isString());
    if ((lval.ref() || rval.ref()) && !equality && !concatenation)
        throw std::runtime_error("Type Error: operator '" + text + "' cannot be applied to " + containerName(lval.ref() ? lval : rval));

    switch (op) {
        case Operator::Add:
            if (concatenation) {
                std::string text;
                size_t known = (lval.isString() ? lval.str().size() : 0) + (rval.isString() ? rval.str().size() : 0);
                if (known > 15) text.reserve(known + 16); // short text stays inside the string, allocating nothing
                appendDisplay(text, lval);
                appendDisplay(text, rval);
                return Value::string(std::move(text));
            }
            if (lval.isFloat() || rval.isFloat())
                return realResult(number(lval, "left", text) + number(rval, "right", text));
            return intResult(integer(lval, "left", text) + integer(rval, "right", text), text);
        case Operator::Sub:
        case Operator::Mul:
        case Operator::Div:
        case Operator::Mod: {
            bool division = op == Operator::Div || op == Operator::Mod;
            if (lval.isFloat() || rval.isFloat()) {
                double l = number(lval, "left", text), r = number(rval, "right", text);
                if (r == 0.0 && division) divisionByZero();
                double out = op == Operator::Sub ? l - r : op == Operator::Mul ? l * r : op == Operator::Div ? l / r : std::fmod(l, r);
                return realResult(out);
            }
            long long l = integer(lval, "left", text), r = integer(rval, "right", text);
            if (r == 0 && division) divisionByZero();
            long long out = op == Operator::Sub ? l - r : op == Operator::Mul ? l * r : op == Operator::Div ? l / r : l % r;
            return intResult(out, text);
        }
        case Operator::Eq: case Operator::Ne: case Operator::Lt: case Operator::Le: case Operator::Gt: case Operator::Ge: {
            auto decide = [&](auto l, auto r) {
                switch (op) {
                    case Operator::Eq: return l == r;
                    case Operator::Ne: return l != r;
                    case Operator::Lt: return l < r;
                    case Operator::Le: return l <= r;
                    case Operator::Ge: return l >= r;
                    default: return l > r;
                }
            };
            bool decided;
            if (lval.isString() && rval.isString()) {
                decided = decide(lval.str(), rval.str());
            } else if (lval.isBool() && rval.isBool()) {
                decided = decide(lval.asBool(), rval.asBool());
            } else if (lval.ref() || rval.ref()) {
                // Arrays, maps and structs are equal when their contents are.
                if (!equality) throw std::runtime_error("Type Error: operator '" + text + "' cannot be applied to " + containerName(lval.ref() ? lval : rval));
                decided = deepEqual(lval, rval) == (op == Operator::Eq);
            } else {
                decided = decide(number(lval, "left", text), number(rval, "right", text));
            }
            return Value::boolean(decided);
        }
        default:
            throw std::runtime_error("Runtime Error: unknown operator '" + text + "'");
    }
}

Value logicalNot(const Value& value) {
    if (!value.isBool()) throw std::runtime_error("Type Error: operand of '!' must be bool, got '" + value.typeName() + "'");
    return Value::boolean(!value.asBool());
}

Value negate(const Value& value) {
    if (value.isInt()) return intResult(-intArgument(value, "operand of unary '-'"), "-");
    if (value.isFloat()) return realResult(-value.asFloat());
    throw std::runtime_error("Type Error: operand of unary '-' must be a number, got '" + value.typeName() + "'");
}

std::string keyOf(const Value& key) {
    if (key.isString()) return key.str();
    if (key.isInt()) return std::to_string(key.asInt());
    throw std::runtime_error("Type Error: a map key must be string or int, got '" + key.typeName() + "'");
}

Value& element(Value& base, const Value& index, bool create) {
    if (!base.is(Value::Kind::Array) && !base.is(Value::Kind::Map))
        throw std::runtime_error("Type Error: '[]' needs an array or a map, got '" + base.typeName() + "'");
    Object& object = *base.ref();
    if (object.kind == Object::Kind::Array) return object.items[positionIn(object, index)];
    // A string key is used as it is; only an int one needs its text made.
    std::string made;
    const std::string& key = index.isString() ? index.str() : (made = keyOf(index));
    if (create) return object.slot(key);
    long at = object.find(key);
    if (at < 0) missingKey(key);
    return object.items[static_cast<size_t>(at)];
}

Value& member(Value& base, const std::string& name, bool create, const std::string** declared) {
    Object* object = base.ref();
    if (base.is(Value::Kind::Struct)) {
        size_t at = fieldIn(*object, name);
        if (declared) *declared = &object->structType->fields[at].type;
        return object->items[at];
    }
    if (base.is(Value::Kind::Map)) {
        if (create) return object->slot(name);
        long at = object->find(name);
        if (at < 0) missingKey(name);
        return object->items[static_cast<size_t>(at)];
    }
    throw std::runtime_error("Type Error: '." + name + "' needs a struct or a map, got '" + base.typeName() + "'");
}

Value postIncrement(Value& target, int delta, const std::string& name) {
    const char* op = delta > 0 ? "++" : "--";
    if (target.isFloat()) {
        Value old = target;
        target = realResult(old.asFloat() + delta);
        return old;
    }
    if (!target.isInt())
        throw std::runtime_error("Type Error: '" + std::string(op) + "' needs an int or float variable, '" + name + "' is " + target.typeName());
    long long value = target.asInt();
    if (value < -2147483648LL || value > 2147483647LL) value = toInt(target, "variable '" + name + "'");
    target = intResult(value + delta, op);
    return Value::integer(value);
}

bool switchMatches(const Value& value, const Value& candidate) {
    double l = 0, r = 0;
    bool numeric = tryNumber(value, l) && tryNumber(candidate, r) && !value.isString() && !candidate.isString();
    return numeric ? l == r : deepEqual(value, candidate);
}

} // namespace skrumi::runtime
