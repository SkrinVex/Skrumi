#pragma once
#include <string>
#include <memory>
#include <vector>
#include <utility>
#include <stdexcept>
#include "skrumi/Context.h"
#include "skrumi/Platform.h"
#include "skrumi/Runtime.h"
#include "skrumi/SourceLocation.h"

namespace skrumi {

namespace bytecode { struct Proto; }

// The parser's tree. The compiler (Compiler.cpp) turns it into bytecode; only the
// declarations below also act on their own, when the code reaches them.
struct Node {
    virtual ~Node() = default;
    SourceRange range;
};

// A statement that registers something in the program: a function, a struct type, a
// module brought in by using or include.
struct Declaration : Node {
    virtual void declare(Context& root) = 0;
    // Whether a function or a struct type of this name already exists: one declared
    // further down a file is registered in advance only when it does not.
    virtual bool exists(const Context& /*root*/) const { return true; }
};

// Where a variable lives, decided once by the resolver (Resolver.cpp) before the code is
// compiled: a numbered slot of the function's frame, a global, or unknown (code the
// resolver has not seen, such as the debugger's console), found by name.
struct VarRef {
    static constexpr int byName = -2;
    static constexpr int global = -1;
    static constexpr int captured = -3; // a variable of an enclosing function, in a lambda
    int slot = byName;
    int capture = -1; // which of the lambda's captured variables, when slot == captured
};

// The numbered slots of a function body or of the program's blocks. A slot that a
// lambda captures is `boxed`: it holds a box that the function and the lambda share.
struct FrameLayout {
    std::vector<std::string> names;
    std::vector<bool> boxed;
    std::vector<std::string> types; // declared type per slot; "" when it takes any value
    std::vector<bool> constants;    // declared const
};

// Where a lambda's captured variable comes from, in the function that creates it: one
// of that function's slots, or one of its own captures when it is a lambda too.
struct Capture {
    bool fromCapture = false;
    int index = 0;
    std::string name;
    std::string type; // the variable's declared type
    bool constant = false;
};

// A bool is the only thing a condition may be; an int used to silently count as false.
inline bool conditionTruth(const Value& value, const char* statement) {
    if (!value.isBool())
        throw std::runtime_error("Type Error: " + std::string(statement) + " condition must be bool, got '" + value.typeName() + "'");
    return value.asBool();
}

// Hot paths must not pay for an error message they will not use: the description of
// the value is assembled only when the value turns out to be unusable.
inline long long intArg(const Value& value, const char* what) {
    if (value.isInt() && value.asInt() >= -2147483648LL && value.asInt() <= 2147483647LL) return value.asInt();
    long long result = 0;
    if (runtime::tryInt(value, result)) return result;
    if (value.isFloat()) throw std::runtime_error("Type Error: " + std::string(what) + " requires int; use to_int()");
    return runtime::toInt(value, what);
}

inline long long intArg(const Value& value, const char* role, const std::string& name) {
    long long result = 0;
    if (runtime::tryInt(value, result)) return result;
    if (value.isFloat()) throw std::runtime_error("Type Error: " + std::string(role) + " requires int; use to_int()");
    return runtime::toInt(value, role + std::string(" '") + name + "'");
}

// Runaway recursion used to kill the process with a stack overflow instead of an error.
// What runs out is the native stack, not a number of calls, and one call costs a
// different amount of it per platform and per compiler: counting frames was wrong on
// Windows, where a thread gets 1 MB rather than the 8 MB Linux gives. Measure the stack.
struct CallDepth {
    // A stack cannot be measured the same way everywhere, so nothing here is trusted
    // alone. The call count starts at a pessimistic four kilobytes per frame and is
    // recalculated from what the stack actually did once there are frames to average;
    // the travelled distance is checked as well. Whichever brake trips first wins.
    static constexpr size_t assumedFrameCost = 4096;
    static constexpr int calibrateAt = 64;
    explicit CallDepth(const std::string& name) : guard_(runtime::stackGuard()) {
        char probe = 0;
        runtime::StackGuard& guard = guard_;
        if (guard.depth == 0) {
            guard.origin = &probe;
            guard.budget = platform::stackBudget();
            guard.limit = static_cast<int>(guard.budget / assumedFrameCost);
        } else {
            std::ptrdiff_t used = guard.origin - &probe; // A stack growing upwards never trips this.
            if (guard.depth == calibrateAt && used > 0) {
                size_t measured = static_cast<size_t>(used) / calibrateAt;
                if (measured > 64) guard.limit = static_cast<int>(guard.budget / measured);
            }
            bool spent = (used > 0 && static_cast<size_t>(used) > guard.budget) || guard.depth >= guard.limit;
#ifdef __EMSCRIPTEN__
            // WebAssembly calls also take the browser's own stack, which cannot be
            // measured and runs out after two or three thousand Skrumi calls.
            spent = spent || guard.depth >= 1000;
#endif
            if (spent) exceeded(name, guard.depth);
        }
        ++guard.depth;
    }
    ~CallDepth() {
        if (--guard_.depth == 0) guard_.origin = nullptr;
    }
    CallDepth(const CallDepth&) = delete;
    CallDepth& operator=(const CallDepth&) = delete;
    // Out of line: the message would otherwise take room in every call's native frame.
    [[noreturn]] static void exceeded(const std::string& name, int depth);

private:
    runtime::StackGuard& guard_;
};

struct BlockNode;

// (int x) => x * 2,  (a, b) => { return a < b; },  x => x + 1: a function as a value. It
// sees the variables around it, not copies of them.
struct LambdaNode : Node {
    std::vector<FuncParam> params; // a parameter without a type takes any value
    std::shared_ptr<BlockNode> body; // an expression body is `{ return expression; }`
    // Set by the resolver: the lambda's own slots (parameters first) and captures.
    std::shared_ptr<FrameLayout> layout;
    std::vector<Capture> captures;
};

struct FuncDefNode : Declaration {
    std::string returnType;
    std::string name;
    std::vector<FuncParam> params;
    std::shared_ptr<Node> body;
    SourceRange nameRange;

    FuncDefNode(std::string rt, std::string n, std::vector<FuncParam> p, std::shared_ptr<Node> b, SourceRange nr = {});

    void declare(Context& root) override;
    bool exists(const Context& root) const override { return root.functions.count(name) > 0; }
    // Calls the function as a call in the program would: the arguments are converted to
    // the parameters' types, the result to the return type. Used by the HTTP server for
    // handlers and by skrumi test.
    Value invoke(ValueList args, Context& caller) const;
    BlockNode* block() const { return block_; }

private:
    BlockNode* block_ = nullptr; // the body, when it is a block (it always is from the parser)
};

struct ReturnNode : Node {
    std::unique_ptr<Node> expr;
    explicit ReturnNode(std::unique_ptr<Node> e) : expr(std::move(e)) {}
};

struct FuncCallNode : Node {
    std::string name;
    std::vector<std::unique_ptr<Node>> args;
    SourceRange nameRange;

    VarRef ref; // a local variable of the name holds the function to call
    FuncCallNode(std::string n, std::vector<std::unique_ptr<Node>> a, SourceRange nr = {})
        : name(std::move(n)), args(std::move(a)), nameRange(nr) {}
};

// base.name(args): a struct's method, or a function stored in a field or map key.
struct MethodCallNode : Node {
    std::unique_ptr<Node> base;
    bool optional = false; // base?.name(args): null when base is null
    std::string name;
    SourceRange nameRange;
    std::vector<std::unique_ptr<Node>> args;
};

// callee(args) where the callee is any expression: make_adder(1)(2), handlers[0]()
struct CallNode : Node {
    std::unique_ptr<Node> callee;
    std::vector<std::unique_ptr<Node>> args;
};

struct NumberNode : Node {
    std::string val;
    bool isFloat;
    explicit NumberNode(std::string v);
    // The number; false for an int whose digits do not fit even 64 bits.
    bool value(Value& out) const {
        out = literal;
        return !tooBig;
    }
private:
    Value literal;
    bool tooBig = false; // digits beyond even 64 bits
};

struct StringNode : Node {
    std::string val;
    explicit StringNode(std::string v) : val(std::move(v)) {}
};

// "Привет, ${name}!": text and expressions, joined as print() shows each value.
struct InterpolationNode : Node {
    std::vector<std::unique_ptr<Node>> parts; // StringNode for the text between expressions
};

struct BoolNode : Node {
    bool val;
    explicit BoolNode(bool v) : val(v) {}
};

struct NullNode : Node {};

// A type written `T?` also holds null.
inline bool isNullable(const std::string& type) { return !type.empty() && type.back() == '?'; }

struct VarAccessNode : Node {
    std::string name;
    SourceRange nameRange;
    VarRef ref;
    explicit VarAccessNode(std::string n, SourceRange nr = {}) : name(std::move(n)), nameRange(nr) {}
};

struct VarDeclNode : Node {
    std::string type, name;
    std::unique_ptr<Node> expr;
    SourceRange nameRange;
    bool global = false;
    bool constant = false;  // const int MAX = 10;
    bool programGlobal = false; // at the program's top level: a register, also found by name
    int slot = VarRef::byName;
    bool duplicate = false; // declared twice in one scope: an error when it runs
    Value::Kind kind;       // what `type` holds, looked up once
    VarDeclNode(std::string t, std::string n, std::unique_ptr<Node> e, SourceRange nr = {}, bool isGlobal = false)
        : type(std::move(t)), name(std::move(n)), expr(std::move(e)), nameRange(nr), global(isGlobal),
          kind(runtime::declaredKind(type)) {}
};

struct VarAssignNode : Node {
    std::string name;
    std::unique_ptr<Node> expr;
    SourceRange nameRange;
    VarRef ref;
    VarAssignNode(std::string n, std::unique_ptr<Node> e, SourceRange nr = {})
        : name(std::move(n)), expr(std::move(e)), nameRange(nr) {}
};

struct BinOpNode : Node {
    std::string op;
    std::unique_ptr<Node> left, right;
    BinOpNode(std::string o, std::unique_ptr<Node> l, std::unique_ptr<Node> r)
        : op(std::move(o)), left(std::move(l)), right(std::move(r)), kind(runtime::operatorOf(op)) {}
    // The operator decided once when the node is built, not by comparing text on every run.
    runtime::Operator kind;
};

// Unary minus and logical not.
struct UnaryOpNode : Node {
    std::string op;
    std::unique_ptr<Node> operand;
    UnaryOpNode(std::string o, std::unique_ptr<Node> n) : op(std::move(o)), operand(std::move(n)) {}
};

// Postfix i++ and i--: returns the old value, stores the new one.
struct PostIncNode : Node {
    std::string name;
    int delta;
    VarRef ref;
    explicit PostIncNode(std::string n, int d = 1) : name(std::move(n)), delta(d) {}
};

// array name = expression;   array name;
struct ArrayDeclNode : Node {
    std::string name;
    std::string type = "array"; // array, array?, array<int>
    std::unique_ptr<Node> initializer;
    SourceRange nameRange;
    bool global = false;
    bool constant = false;
    bool programGlobal = false;
    int slot = VarRef::byName;
    bool duplicate = false;
    ArrayDeclNode(std::string n, std::unique_ptr<Node> init = nullptr, SourceRange nr = {})
        : name(std::move(n)), initializer(std::move(init)), nameRange(nr) {}
};

// [a, b, c] creates a temporary array owned by the scope that evaluates it.
struct ArrayLiteralNode : Node {
    std::vector<std::unique_ptr<Node>> elements;
};

// base[index]: an array element by position or a map value by key.
struct IndexNode : Node {
    std::unique_ptr<Node> base, index;
    IndexNode(std::unique_ptr<Node> b, std::unique_ptr<Node> i) : base(std::move(b)), index(std::move(i)) {}
};

// base.name: a struct field, or a map value whose key is a plain word.
struct FieldNode : Node {
    std::unique_ptr<Node> base;
    bool optional = false; // base?.name: null when base is null
    std::string name;
    SourceRange nameRange;
    FieldNode(std::unique_ptr<Node> b, std::string n, SourceRange nr = {})
        : base(std::move(b)), name(std::move(n)), nameRange(nr) {}
};

// target = value (also +=, -=, *=, /=, %=), where the target is a chain of indexes and
// fields that starts at a variable: items[i] = 1;  user.name = "Ann";  m["a"][0] += 2;
struct SetNode : Node {
    std::unique_ptr<Node> target, value;
    std::string op;
    SetNode(std::unique_ptr<Node> t, std::unique_ptr<Node> v, std::string o = "=")
        : target(std::move(t)), value(std::move(v)), op(std::move(o)) {}
};

// {"key": value, ...} creates a map.
struct MapLiteralNode : Node {
    std::vector<std::pair<std::unique_ptr<Node>, std::unique_ptr<Node>>> entries;
};

// struct Name { type field; type field = default; }
struct StructDefNode : Declaration {
    std::shared_ptr<StructType> type;
    SourceRange nameRange;
    std::vector<SourceRange> fieldRanges;
    std::vector<std::shared_ptr<FuncDefNode>> methods; // also in type->methods, in source order
    void declare(Context& root) override;
    bool exists(const Context& root) const override { return root.structs.count(type->name) > 0; }
};

// enum Color { Red, Green, Blue }  enum Status { Active = "active", Blocked = "blocked" }
// Color.Red is a fixed value of type Color: .name is "Red", .value 0 (or the given one).
struct EnumDefNode : Declaration {
    std::shared_ptr<StructType> type; // fields name and value, isEnum
    SourceRange nameRange;
    struct Member {
        std::string name;
        Value value;
        SourceRange range;
    };
    std::vector<Member> members;
    void declare(Context& root) override;
    bool exists(const Context& root) const override { return root.structs.count(type->name) > 0; }
};

// try { ... } catch (string error) { ... } finally { ... }
struct TryNode : Node {
    std::unique_ptr<Node> body, handler, cleanup;
    std::string errorName;
    SourceRange errorRange;
    int errorSlot = VarRef::byName;
};

// throw expression;  raises an error with the value as its message.
struct ThrowNode : Node {
    std::unique_ptr<Node> message;
    explicit ThrowNode(std::unique_ptr<Node> m) : message(std::move(m)) {}
};

struct BlockNode : Node {
    std::vector<std::unique_ptr<Node>> stmts;
    const std::string* file = nullptr; // Source file of the statements, for error locations.
    // The program and an imported module are the global scope itself, not a block inside it.
    bool scoped = true;
    // Slots declared inside this block, emptied when it ends; the layout of a function
    // body or of the program, set by the resolver.
    int firstSlot = 0, endSlot = 0;
    std::shared_ptr<FrameLayout> layout;
    // A function body's bytecode, compiled at its first call; the second version reports
    // statements and scopes to a debugger.
    std::shared_ptr<bytecode::Proto> proto, debugProto;
};

struct IfNode : Node {
    std::unique_ptr<Node> condition, thenB, elseB;
    IfNode(std::unique_ptr<Node> c, std::unique_ptr<Node> t, std::unique_ptr<Node> e = nullptr)
        : condition(std::move(c)), thenB(std::move(t)), elseB(std::move(e)) {}
};

struct WhileNode : Node {
    std::unique_ptr<Node> condition, body;
    WhileNode(std::unique_ptr<Node> c, std::unique_ptr<Node> b)
        : condition(std::move(c)), body(std::move(b)) {}
};

struct ForNode : Node {
    std::unique_ptr<Node> init, condition, step, body;
    int firstSlot = 0, endSlot = 0; // the loop's own variables
    ForNode(std::unique_ptr<Node> i, std::unique_ptr<Node> c, std::unique_ptr<Node> s, std::unique_ptr<Node> b)
        : init(std::move(i)), condition(std::move(c)), step(std::move(s)), body(std::move(b)) {}
};

// for (x in items), for (key, value in ages), for (int i, string ch in text): each
// variable with an optional type. Arrays give elements (or index and element), maps keys
// (or key and value), strings characters (or their number and the character).
struct ForInNode : Node {
    struct Variable {
        std::string type; // empty: the variable takes whatever the loop gives it
        std::string name;
        SourceRange range;
        int slot = VarRef::byName;
    };
    std::vector<Variable> variables; // one or two
    std::unique_ptr<Node> iterable, body;
    int firstSlot = 0, endSlot = 0;
};

struct BreakNode : Node {};

struct ContinueNode : Node {};

struct SwitchNode : Node {
    std::unique_ptr<Node> expr;
    std::vector<std::pair<std::unique_ptr<Node>, std::unique_ptr<Node>>> cases;
    std::unique_ptr<Node> defaultCase;

    explicit SwitchNode(std::unique_ptr<Node> e) : expr(std::move(e)) {}
};

// Forward declaration of interpreter execution hooks
void executeIncludeHook(const std::string& path, Context& ctx, const std::string& currentFile, bool importOnly);
void executeUsingHook(const std::string& libName, Context& ctx, const std::string& currentFile, const std::string& alias);

struct UsingNode : Declaration {
    std::string libName;
    std::string currentFile;
    std::string alias; // using math as m;
    SourceRange aliasRange;
    UsingNode(std::string lib, std::string curFile = "")
        : libName(std::move(lib)), currentFile(std::move(curFile)) {}
    void declare(Context& root) override { executeUsingHook(libName, root, currentFile, alias); }
};

struct IncludeNode : Declaration {
    std::string filename;
    std::string currentFile;
    IncludeNode(std::string file, std::string curFile = "")
        : filename(std::move(file)), currentFile(std::move(curFile)) {}
    void declare(Context& root) override { executeIncludeHook(filename, root, currentFile, true); }
};

} // namespace skrumi
