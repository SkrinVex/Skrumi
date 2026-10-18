#pragma once
#include <exception>
#include <string>
#include <vector>
#include "skrumi/Context.h"
#include "skrumi/Builtins.h"

namespace skrumi {
namespace runtime {

// A builtin is its catalog entry plus the C++ that implements it.
struct Builtin;
const Builtin* findBuiltin(const std::string& name);
const BuiltinSpec& specOf(const Builtin& builtin);
// Invoke this builtin; mismatched arguments are errors, never overload fallback.
Value invoke(const Builtin& builtin, Arguments args, Context& ctx);

bool isBuiltin(const std::string& name);
// Calls a function value (a func): a Skrumi function, a builtin or a lambda. Builtins
// that take a function, such as array_map, call it through this.
Value callValue(const Value& function, Arguments args, Context& ctx);
// The same with arguments that the call may move from.
Value callValue(const Value& function, Value* args, size_t count, Context& ctx);
Value callBuiltin(const std::string& name, Arguments args, Context& ctx);

int getLogLevelThreshold();
std::string formatNumber(double val);

// Recursion guard state. Kept in one translation unit rather than as inline
// thread_local data in a header, which compilers disagree about.
struct StackGuard {
    int depth = 0;
    int limit = 0;
    const char* origin = nullptr;
    size_t budget = 0;
    int line = 0;                     // Line of the statement being executed, for the error report,
    const std::string* file = nullptr; // and its source file (an interned name, never freed).
    int tryDepth = 0;                  // try blocks the running code is inside of
    // Calls the bytecode VM runs without a native frame of their own (see execute in
    // Vm.cpp): they cost no native stack, so they have a limit of their own.
    int vmDepth = 0;
    // The bytecode VM learns the line of an error from the innermost function it leaves;
    // the error it last placed is kept so that outer functions do not move it. It is
    // known by its message (a hash of it): MSVC hands every frame a copy of the
    // exception, so pointers to it never compare equal. A catch that takes the error
    // forgets it. Only plain fields here: the per-thread state then needs no
    // construction check each time a call reaches it.
    bool placed = false;
    std::size_t placedMessage = 0;
};
StackGuard& stackGuard();

// Numeric access to values: a number as it is, a string by parsing its text. These
// report the offending text and the place that asked for it. Probes build no message;
// the *what* overloads below describe the failure and are only reached when a value
// really is wrong.
bool tryNumber(const Value& value, double& out);
bool tryInt(const Value& value, long long& out);

double toNumber(const Value& value, const std::string& what);
int toInt(const Value& value, const std::string& what);
// An int value made from any number, truncating a float, checked to fit in int.
Value intValue(const Value& value, const std::string& what);
// Results of arithmetic: an int must fit in int, a float must be finite.
[[noreturn]] void intOverflow(long long result, const char* op);
inline Value intResult(long long result, const char* op) {
    if (result < -2147483648LL || result > 2147483647LL) intOverflow(result, op);
    return Value::integer(result);
}
inline Value intResult(long long result, const std::string& op) { return intResult(result, op.c_str()); }
Value realResult(double result);
// A value of a scalar type from its text, as the embedding API and the debugger
// receive it: "42" for int, "2.5" for float, "true" for bool, anything for string.
Value parseScalar(const std::string& type, const std::string& text, const std::string& what);

// A binary operator, decided once from its text ("+", "+=", "<"); Unknown for anything else.
enum class Operator : unsigned char { Add, Sub, Mul, Div, Mod, Eq, Ne, Lt, Le, Gt, Ge, And, Or, Unknown };
Operator operatorOf(const std::string& text);

// Converts a value for storage in a slot of the given type: a variable, a parameter
// or a return value. int and float convert both ways (float to int truncates, and
// must fit), any scalar becomes text in a string slot, everything else is an error.
void coerce(const std::string& type, Value& value, const std::string& what);
// The kind of value a declared type holds: Struct for the name of a struct, Void for
// "void". Declarations look it up once, so storing checks a byte instead of a name.
Value::Kind declaredKind(const std::string& type);
// For array<T> and map<string,T>: whether the type is one, and its element type T.
bool containerType(const std::string& type, Value::Kind& kind, std::string& element);
// A value's type as messages show it: array<int> for a typed array.
std::string typeText(const Value& value);
// A value about to be stored in a typed container's element: converted to its type.
void storeElement(const Object& container, Value& value);
// True when a value can be stored as it is in a slot of the declared kind; otherwise
// coerce() converts it or reports the mismatch. A struct always takes the slow path,
// because its name has to match too.
inline bool storesAsIs(Value::Kind declared, const Value& value) {
    if (value.kind() != declared || declared == Value::Kind::Struct) return false;
    return declared != Value::Kind::Int || (value.asInt() >= -2147483648LL && value.asInt() <= 2147483647LL);
}

// A value as print() shows it: numbers and text as they are, arrays as [1, 2],
// maps as {key: value}, structs as Name{field: value}.
std::string display(const Value& value);
// The value a slot of this type starts with: 0, 0.0, "", false, an empty array or
// map, a struct with its defaults. Unknown types are an error.
Value zeroValue(const std::string& type, Context& ctx);
// A struct value built from positional arguments; missing trailing fields take their
// defaults.
Value construct(const StructType& type, ValueList args, Context& ctx);
// The same from arguments in place (moved from), for a type already looked up.
Value construct(const std::shared_ptr<const StructType>& type, Value* args, size_t count, Context& ctx);

// JSON navigation by a dotted path. Object keys and array indexes are both path
// segments: "message.chat.id", "items.0.name". The empty path is the document itself.
Value jsonGet(const std::string& json, const std::string& path);
Value jsonEscape(const std::string& text);
// The same escaping appended to out, without a string of its own.
void appendJsonEscaped(std::string& out, const std::string& text);
// A whole JSON document as Skrumi values in one pass: objects become maps, arrays
// arrays, whole numbers int (float beyond int's range), other numbers float, null null.
// `what` names the builtin in errors.
Value parseJson(const std::string& text, const char* what);
int jsonCount(const std::string& json, const std::string& path);
std::string jsonType(const std::string& json, const std::string& path);
// The value at a path as JSON text (a string keeps its quotes); false if absent.
bool jsonRaw(const std::string& json, const std::string& path, std::string& out);
// A copy of the document with raw JSON stored at the path. Missing object keys are
// created; an array index may name an element or the position right after the last.
std::string jsonSet(const std::string& json, const std::string& path, const std::string& raw);
bool jsonValid(const std::string& json);
// Members of an object (key, raw value) or elements of an array ("", raw value), in
// document order. Empty for anything else.
std::vector<std::pair<std::string, std::string>> jsonEntries(const std::string& json);

// Mustache-style HTML templates over a JSON document: {{path}} (HTML-escaped),
// {{{path}}} (raw), {{#each path}}...{{/each}}, {{#if path}}...{{else}}...{{/if}}.
std::string renderTemplate(const std::string& source, const std::string& json);

// A stable pointer for a source file name; AST blocks keep it to report errors.
const std::string* internFile(const std::string& name);
// A source identity as a person wants to read it: relative to the working directory
// when the file is inside it, and std/x.skm for the embedded standard library.
std::string displayPath(const std::string& identity);
// "file:line: message" for a runtime error at the statement last entered.
std::string locate(const std::string& message, const std::string& fallbackFile);

void loadDotEnv(const std::string& scriptPath);
std::string resolveSourceFile(const std::string& requested, const std::string& currentFile, const std::string& skrumiHome);

} // namespace runtime
} // namespace skrumi
