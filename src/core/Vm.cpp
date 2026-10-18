#ifdef __ANDROID__
#include "skrumi/Android.h"
#define SKRUMI_CHECK_CANCEL() do { if (!(++androidTicks & 1023u)) skrumi::android::checkpoint(); } while (0)
#else
#define SKRUMI_CHECK_CANCEL() ((void)0)
#endif
// Runs bytecode (Compiler.cpp). A Skrumi call is a native call of execute(), so the
// native stack is what the call depth limit measures, as it always has. Registers live
// on a stack of fixed chunks: a frame's registers never move while it runs, so the
// debugger may point at them.
#include "skrumi/Bytecode.h"
#include "skrumi/AST.h"
#include "skrumi/Debug.h"
#include "skrumi/Platform.h"
#include "Operations.h"
#include "builtins/Builtin.h"
#include <algorithm>
#include <memory>
#include <stdexcept>
#include <vector>

#if defined(_MSC_VER)
#define SKRUMI_COLD __declspec(noinline)
#define SKRUMI_APART __declspec(noinline)
#else
#define SKRUMI_COLD __attribute__((noinline, cold))
#define SKRUMI_APART __attribute__((noinline))
#endif

namespace skrumi::vm {

using namespace bytecode;

namespace {

// ---------------------------------------------------------------- registers

struct RegisterStack {
    std::vector<std::unique_ptr<Value[]>> chunks;
    std::vector<size_t> sizes;
    size_t chunk = 0;
    Value* top = nullptr;
    Value* limit = nullptr;
};

thread_local RegisterStack registers;

// The registers of one frame, all empty when it begins and emptied again when it ends,
// and where the stack stood before them. Frames open and close in stack order.
struct Registers {
    Value* base = nullptr;
    size_t size = 0;
    Value* top;
    Value* limit;
    size_t chunk;
};

void nextChunk(size_t size);

inline Registers openRegisters(size_t size) {
    RegisterStack& stack = registers;
    Registers frame{nullptr, size, stack.top, stack.limit, stack.chunk};
    if (!stack.top || stack.top + size > stack.limit) nextChunk(size);
    frame.base = stack.top;
    stack.top += size;
    return frame;
}

inline void popRegisters(const Registers& frame) {
    RegisterStack& stack = registers;
    stack.top = frame.top;
    stack.limit = frame.limit;
    stack.chunk = frame.chunk;
}

inline void closeRegisters(const Registers& frame) {
    for (size_t i = 0; i < frame.size; ++i) frame.base[i].reset();
    popRegisters(frame);
}

// The end of a frame execute() ran itself: what the registers hold is let go of, but
// numbers stay behind. Every frame writes a register before reading it; only code that
// shows registers as they are (the debugger, the program's top level) clears them first.
inline void leaveRegisters(const Registers& frame) {
    for (Value* at = frame.base, *end = frame.base + frame.size; at != end; ++at)
        if (at->kind() >= Value::Kind::String) at->reset();
    popRegisters(frame);
}

class Window {
public:
    explicit Window(size_t size) : frame_(openRegisters(size)) {}
    ~Window() { closeRegisters(frame_); }
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;
    Value* base() const { return frame_.base; }

private:
    Registers frame_;
};

void nextChunk(size_t size) {
    // Bounded, so the allocation below cannot be asked for the whole address space.
    if (size > (size_t{1} << 28)) throw std::length_error("register window too large");
    RegisterStack& stack = registers;
    size_t next = stack.top ? stack.chunk + 1 : 0;
    if (next < stack.chunks.size() && stack.sizes[next] < size) {
        stack.chunks.resize(next);
        stack.sizes.resize(next);
    }
    if (next >= stack.chunks.size()) {
        size_t capacity = std::max<size_t>(size, size_t{1} << 14);
        stack.chunks.emplace_back(new Value[capacity]);
        stack.sizes.push_back(capacity);
    }
    stack.chunk = next;
    stack.top = stack.chunks[next].get();
    stack.limit = stack.top + stack.sizes[next];
}

// ---------------------------------------------------------------- debugger

// What a debugger sees of a running frame: a scope for the function (its slots are
// the registers) and one for every block open inside it.
struct DebugFrame {
    DebugHook* hook;
    Context* function;
    std::vector<std::unique_ptr<Context>> owned;
    std::vector<Context*> chain;
    Context* current() { return chain.empty() ? function : chain.back(); }

    void enter(bool scoped) {
        Context* scope = current();
        // A program's top level is the root scope itself, not a block inside it.
        if (scoped) {
            owned.push_back(std::make_unique<Context>(*scope));
            scope = owned.back().get();
        } else {
            owned.push_back(nullptr);
        }
        chain.push_back(scope);
        hook->enterScope(*scope);
    }
    void leave() {
        hook->leaveScope();
        chain.pop_back();
        owned.pop_back();
    }
    void leaveTo(size_t depth) {
        while (chain.size() > depth) leave();
    }
};

// ---------------------------------------------------------------- helpers

// A variable of the program's top level, now declared: found by name from now on.
SKRUMI_COLD void declareProgramGlobal(const GlobalSite& site, Value& slot, Context& root) {
    root.programGlobals[site.name] = slot.is(Value::Kind::Box) ? &slot.ref()->items[0] : &slot;
    if (site.declaresConstant) root.constants.insert(site.name);
    if (!site.nullable.empty()) root.nullableGlobals[site.name] = site.nullable;
}

SKRUMI_COLD void markDeclared(DebugFrame& frame, int from, int to, bool declared) {
    auto& marks = frame.function->declared;
    for (int i = from; i < to && i >= 0 && static_cast<size_t>(i) < marks.size(); ++i) marks[static_cast<size_t>(i)] = declared;
}

[[noreturn]] SKRUMI_COLD void notFound(const std::string& name) {
    throw std::runtime_error("Runtime Error: Variable '" + name + "' not found!");
}

// A global by name: a variable of the root, or one of the running program's top level.
Value* globalVariable(Context& root, const std::string& name) {
    auto found = root.variables.find(name);
    if (found != root.variables.end()) return &found->second;
    if (root.programGlobals.empty()) return nullptr;
    auto program = root.programGlobals.find(name);
    return program == root.programGlobals.end() ? nullptr : program->second;
}

SKRUMI_APART Value* lookUp(GlobalSite& site, Context& root, Context& scope) {
    if (site.byName) {
        Value* found = scope.findVar(site.name);
        if (!found) notFound(site.name);
        // The debugger's console reaches globals by name; a constant stays one there too.
        Context& owner = *scope.getRoot();
        auto global = owner.variables.find(site.name);
        site.constant = global != owner.variables.end() && &global->second == found && owner.constants.count(site.name) > 0;
        return found;
    }
    Value* variable = globalVariable(root, site.name);
    if (!variable) notFound(site.name);
    site.constant = !root.constants.empty() && root.constants.count(site.name) > 0;
    site.cached = variable;
    site.root = &root;
    site.generation = root.generation;
    return site.cached;
}

// The name of a function, or of a builtin, read as a value: func f = add;
Value functionValue(const std::string& name, Context& root) {
    auto function = root.getFunc(name);
    const runtime::Builtin* builtin = function ? nullptr : runtime::findBuiltin(name);
    if (!function && !builtin) return Value();
    auto callee = std::make_shared<Callee>();
    callee->name = name;
    callee->function = function;
    callee->builtin = builtin;
    Value value = Value::container(Value::Kind::Function);
    value.ref()->callee = std::move(callee);
    return value;
}

// A name read as a value: a variable, or else a function of that name.
SKRUMI_APART Value* lookUpValue(GlobalSite& site, Context& root, Context& scope) {
    Value* variable = site.byName ? scope.findVar(site.name) : nullptr;
    if (!site.byName) {
        if (Value* found = globalVariable(root, site.name)) {
            site.constant = !root.constants.empty() && root.constants.count(site.name) > 0;
            site.cached = found;
            site.root = &root;
            site.generation = root.generation;
            return site.cached;
        }
    }
    if (variable) return variable;
    site.function = functionValue(site.name, root);
    if (site.function.isVoid()) notFound(site.name);
    return &site.function;
}

inline Value* readGlobal(GlobalSite& site, Context& root, Context& scope) {
    if (site.cached && site.root == &root && site.generation == root.generation) return site.cached;
    return lookUpValue(site, root, scope);
}

inline Value* global(GlobalSite& site, Context& root, Context& scope) {
    if (site.cached && site.root == &root && site.generation == root.generation) return site.cached;
    return lookUp(site, root, scope);
}

bool fitsInt(long long value) { return value >= -2147483648LL && value <= 2147483647LL; }

int depthAt(const Proto& proto, int pc) {
    int depth = 0;
    for (const auto& body : proto.tryBodies)
        if (pc >= body.first && pc < body.second) ++depth;
    return depth;
}

const Handler* handlerFor(const Proto& proto, int pc, bool runtimeError) {
    const Handler* best = nullptr;
    for (const auto& handler : proto.handlers) {
        if (pc < handler.start || pc >= handler.end) continue;
        if (!handler.finally && !runtimeError) continue;
        if (!best || handler.level > best->level) best = &handler;
    }
    return best;
}

const char* statementName(int kind) {
    return kind == 0 ? "if" : kind == 1 ? "while" : "for";
}

bool truth(const Value& value, int kind) {
    if (value.isBool()) return value.asBool();
    return conditionTruth(value, statementName(kind)); // raises the error
}

SKRUMI_APART void setPath(Proto& proto, const SetPath& path, Value* R, Value& assigned, Context& root, Context& scope,
                           Object* closure) {
    Value* at;
    if (path.slot >= 0) at = path.boxed ? &R[path.slot].ref()->items[0] : &R[path.slot];
    else if (path.capture >= 0) at = &closure->items[static_cast<size_t>(path.capture)].ref()->items[0];
    else at = global(proto.globals[static_cast<size_t>(path.global)], root, scope);
    const std::string* declared = nullptr;
    const Object* owner = nullptr; // the container of the element written
    bool create = path.op == "=";
    for (size_t i = 0; i < path.steps.size(); ++i) {
        const auto& step = path.steps[i];
        bool last = i + 1 == path.steps.size();
        if (at->ref() && at->ref()->frozen)
            throw std::runtime_error("Runtime Error: the values of an enum cannot be changed");
        owner = at->ref();
        at = step.field ? &runtime::member(*at, step.name, last && create, last ? &declared : nullptr)
                        : &runtime::element(*at, R[step.key], last && create);
    }
    Value value = create ? assigned : runtime::binary(path.kind, path.op, *at, assigned);
    // A struct field keeps its declared type; elements and map values take any value.
    if (declared) runtime::coerce(*declared, value, "field '" + path.steps.back().name + "'");
    else if (owner && owner->elementType) runtime::storeElement(*owner, value); // array<int>, map<string,int>
    *at = std::move(value);
}

Value execute(Proto& proto, Value* R, Context& root, Context& scope, DebugFrame* debug, Object* closure = nullptr);

SKRUMI_COLD const Proto& compile(const FuncDefNode& function, bool debug) {
    auto& slot = debug ? function.block()->debugProto : function.block()->proto;
    slot = compileFunction(function, debug);
    return *slot;
}

inline const Proto& protoOf(const FuncDefNode& function, bool debug) {
    const auto& slot = debug ? function.block()->debugProto : function.block()->proto;
    return slot ? *slot : compile(function, debug);
}

// A call under a debugger, kept apart so that an ordinary call's native frame stays
// small: the depth of recursion a program gets depends on it.
SKRUMI_COLD Value executeDebugged(Proto& proto, Value* R, Context& root, DebugHook* hook, Object* closure = nullptr) {
    // The debugger shows registers as they are: none may hold what an earlier frame left.
    for (int i = static_cast<int>(proto.params.size()); i < proto.registers; ++i) R[i].reset();
    // Lexical scope: a function sees globals, never the caller's locals.
    Context scope;
    scope.parent = &root;
    scope.interpreter = root.interpreter;
    scope.slots = R;
    scope.slotNames = proto.slotNames.get();
    scope.frame = true;
    scope.declared.assign(static_cast<size_t>(proto.slots), 0);
    for (size_t i = 0; i < proto.params.size() && i < scope.declared.size(); ++i) scope.declared[i] = 1;
    DebugFrame frame{hook, &scope, {}, {}};
    struct Leave {
        DebugFrame& frame;
        ~Leave() {
            frame.leaveTo(0);
            frame.hook->leaveFunction();
        }
    };
    hook->enterFunction(proto.name, proto.file, proto.line, scope);
    Leave leave{frame};
    return execute(proto, R, root, root, &frame, closure);
}

SKRUMI_COLD void convert(const Conversion& conversion, Value& value) {
    runtime::coerce(conversion.type, value, conversion.what);
}

[[noreturn]] SKRUMI_COLD void wrongCount(const Proto& proto, size_t count) {
    size_t hidden = proto.method ? 1 : 0; // a method's `this`
    throw std::runtime_error("Runtime Error: " + std::string(proto.method ? "method '" : "function '") + proto.name +
                             "' expects " + std::to_string(proto.params.size() - hidden) + " arguments, got " +
                             std::to_string(count - hidden));
}

[[noreturn]] SKRUMI_COLD void noResult(const Proto& proto) {
    throw std::runtime_error("Runtime Error: function '" + proto.name + "' must return " + proto.result.type +
                             " but ended without a value");
}

SKRUMI_APART void refresh(CallSite& site, Context& root);

// Calls the VM runs in one loop use heap memory, not the native stack: this bounds
// the recursion of a program that never stops.
constexpr int maxVmDepth = 100000;

// For each comparison operator, the outcomes it accepts: bit 0 less, bit 1 equal,
// bit 2 greater. Indexed by runtime::Operator.
struct CompareMasks {
    unsigned char bits[256] = {}; // Instr::x is a byte
    constexpr CompareMasks() {
        bits[static_cast<int>(runtime::Operator::Lt)] = 0b001;
        bits[static_cast<int>(runtime::Operator::Le)] = 0b011;
        bits[static_cast<int>(runtime::Operator::Eq)] = 0b010;
        bits[static_cast<int>(runtime::Operator::Ne)] = 0b101;
        bits[static_cast<int>(runtime::Operator::Gt)] = 0b100;
        bits[static_cast<int>(runtime::Operator::Ge)] = 0b110;
    }
    constexpr unsigned char operator[](int op) const { return bits[op]; }
};
constexpr CompareMasks compareMask;

// A caller that execute() left for a function it runs itself, and what to go back to.
struct Caller {
    Proto* proto;
    const Instr* ip; // where the caller goes on
    Value* R;
    Object* closure;
    Context* scope;
    std::exception_ptr* pending; // the caller's (owned by the caller while it waits)
    int result;      // the caller's register that takes the result
    Registers callee;
};

// The two builtins loops call most, known by address so that CallDirect runs their
// usual case in place (the way CPython specializes list.append).
const runtime::Builtin* const builtinPush = runtime::findBuiltin("push");
const runtime::Builtin* const builtinSize = runtime::findBuiltin("size");

// The builtin fast path of a CallDirect: its arguments stay in their registers.
SKRUMI_APART bool fastDirect(const runtime::Builtin& builtin, Value* R, const std::int32_t* registersOfArgs, size_t count,
                              Value& result) {
    Value* argv[4];
    for (size_t i = 0; i < count; ++i) argv[i] = &R[registersOfArgs[i]];
    return builtin.fast(argv, count, result);
}

// A CallDirect to a function that only passes its parameters on to a builtin (see
// Proto::forward) straight to the builtin's fast path: false when that does not apply.
SKRUMI_APART bool forwardDirect(Proto& callee, Value* R, const std::int32_t* registersOfArgs, size_t count, Value& result,
                                 Context& root) {
    if (count != callee.params.size() || count > 4) return false;
    CallSite& site = callee.calls[static_cast<size_t>(callee.forward)];
    if (!site.resolved || site.root != &root || site.generation != root.functionGeneration) refresh(site, root);
    if (!site.builtin || site.function || !site.builtin->fast) return false;
    Value* argv[4];
    for (size_t i = 0; i < count; ++i) {
        argv[i] = &R[registersOfArgs[i]];
        const Conversion& param = callee.params[i];
        if (!param.type.empty() && !runtime::storesAsIs(param.kind, *argv[i])) return false;
    }
    if (!site.builtin->fast(argv, count, result)) return false;
    // The forwarder's own result conversion, as callFunction does it.
    if (callee.result.kind == Value::Kind::Void) result.reset();
    else if (result.isVoid()) {
        if (!isNullable(callee.result.type)) noResult(callee);
    } else if (!runtime::storesAsIs(callee.result.kind, result)) {
        convert(callee.result, result);
    }
    return true;
}

// Arguments of a call execute() runs itself that need converting, converted in the
// caller's row at R[in.a] before the callee's frame opens: an error then leaves nothing
// half done. A CallDirect's arguments are all copied into that row first.
SKRUMI_COLD void convertArguments(const Proto& callee, Value* args, const Value* R, const std::int32_t* direct,
                                   size_t count) {
    for (size_t i = 0; i < count; ++i) {
        if (direct) args[i] = R[direct[i]];
        const Conversion& param = callee.params[i];
        if (!param.type.empty() && !runtime::storesAsIs(param.kind, args[i])) convert(param, args[i]);
    }
}

// Opens the frame of a Skrumi function that execute() runs itself: the arguments are
// checked and converted in the caller, then go into the new registers.
// A CallDirect's arguments are the caller's registers listed in `direct` (copied: they
// may be variables); a Call's sit in a row from R[in.a] (moved: they are temporaries).
// Registers above the top of the register stack hold no string or container (frames
// let those go when they end), so the new frame's registers are written over as they are.
SKRUMI_APART Value* enterCall(std::vector<Caller>& calls, Proto& callee, const Instr& in, Proto* proto, const Instr* ip,
                               Value* R, Object* closure, Context* scope, std::unique_ptr<std::exception_ptr[]>& pending,
                               runtime::StackGuard& guard, const std::int32_t* direct = nullptr) {
    size_t count = static_cast<size_t>(in.c);
    if (count != callee.params.size()) wrongCount(callee, count);
    Value* args = R + in.a;
    const Conversion* params = callee.params.data();
    for (size_t i = 0; i < count; ++i) {
        const Value& arg = direct ? R[direct[i]] : args[i];
        if (!params[i].type.empty() && !runtime::storesAsIs(params[i].kind, arg)) {
            convertArguments(callee, args, R, direct, count);
            direct = nullptr; // the arguments are in the row now
            break;
        }
    }
    if (++guard.vmDepth > maxVmDepth) {
        --guard.vmDepth;
        CallDepth::exceeded(callee.name, guard.vmDepth + guard.depth);
    }
    if (calls.capacity() == calls.size()) calls.reserve(calls.empty() ? 64 : calls.size() * 2);
    Registers registersOfCallee = openRegisters(static_cast<size_t>(callee.registers)); // may throw: before anything moves
    calls.push_back(Caller{proto, ip, R, closure, scope, pending.release(), in.a, registersOfCallee});
    Value* frame = registersOfCallee.base;
    if (direct) {
        for (size_t i = 0; i < count; ++i) new (frame + i) Value(R[direct[i]]);
    } else {
        for (size_t i = 0; i < count; ++i) new (frame + i) Value(std::move(args[i]));
    }
    if (callee.pendingErrors > 0) pending.reset(new std::exception_ptr[static_cast<size_t>(callee.pendingErrors)]);
    const Value* constants = callee.constants.data();
    for (const auto& constant : callee.preload) new (frame + constant.first) Value(constants[constant.second]);
    return frame;
}

// A builtin's fast path (see FastPath) for arguments that sit in a row.
inline bool fastCall(const runtime::Builtin& builtin, Value* args, size_t count, Value& result) {
    if (!builtin.fast || count > 4) return false;
    Value* argv[4];
    for (size_t i = 0; i < count; ++i) argv[i] = args + i;
    return builtin.fast(argv, count, result);
}

// Arguments that need no conversion go straight on to the builtin a forwarding body
// calls: no frame, and an error names the line of the call made to the forwarder.
SKRUMI_APART const runtime::Builtin* forwardsTo(Proto& proto, const Value* args, size_t count, Context& root) {
    if (proto.forward < 0) return nullptr;
    CallSite& site = proto.calls[static_cast<size_t>(proto.forward)];
    if (!site.resolved || site.root != &root || site.generation != root.functionGeneration) refresh(site, root);
    if (!site.builtin || site.function) return nullptr;
    for (size_t i = 0; i < count; ++i) {
        const Conversion& param = proto.params[i];
        if (!param.type.empty() && !runtime::storesAsIs(param.kind, args[i])) return nullptr;
    }
    return site.builtin;
}

// Apart, so that the value the builtin returns takes no room in every call's frame.
SKRUMI_APART bool forwardTo(Proto& proto, Value* args, size_t count, Context& root, Value& result) {
    const runtime::Builtin* builtin = forwardsTo(proto, args, count, root);
    if (!builtin) return false;
    if (fastCall(*builtin, args, count, result)) return true;
    result = runtime::invoke(*builtin, Arguments(args, count), root);
    return true;
}

SKRUMI_APART Value callFunction(const FuncDefNode& function, Value* args, size_t count, Context& root) {
    DebugHook* hook = runtime::debugHook();
    Proto& proto = const_cast<Proto&>(protoOf(function, hook != nullptr));
    if (count != proto.params.size()) wrongCount(proto, count);
    Value result;
    if (proto.forward >= 0 && forwardTo(proto, args, count, root, result)) {
    } else {
        Window window(static_cast<size_t>(proto.registers));
        Value* R = window.base();
        for (size_t i = 0; i < count; ++i) {
            R[i] = std::move(args[i]);
            const Conversion& param = proto.params[i];
            if (!param.type.empty() && !runtime::storesAsIs(param.kind, R[i])) convert(param, R[i]);
        }
        CallDepth depth(proto.name);
        result = hook ? executeDebugged(proto, R, root, hook) : execute(proto, R, root, root, nullptr);
    }
    if (proto.result.kind == Value::Kind::Void) return Value();
    if (result.isVoid()) {
        if (isNullable(proto.result.type)) return result; // a T? function may return null
        noResult(proto);
    }
    if (!runtime::storesAsIs(proto.result.kind, result)) convert(proto.result, result);
    return result;
}

Value callLambda(Object& function, Value* args, size_t count, Context& root) {
    Proto& proto = *function.callee->lambda;
    if (count != proto.params.size()) wrongCount(proto, count);
    DebugHook* hook = runtime::debugHook();
    Window window(static_cast<size_t>(proto.registers));
    Value* R = window.base();
    for (size_t i = 0; i < count; ++i) {
        R[i] = std::move(args[i]);
        const Conversion& param = proto.params[i];
        if (!param.type.empty() && !runtime::storesAsIs(param.kind, R[i])) convert(param, R[i]);
    }
    CallDepth depth(proto.name);
    return hook ? executeDebugged(proto, R, root, hook, &function) : execute(proto, R, root, root, nullptr, &function);
}

[[noreturn]] SKRUMI_COLD void notFunction(const std::string& name, const Value& value) {
    throw std::runtime_error("Type Error: '" + name + "' is not a function, it holds a value of type '" + value.typeName() + "'");
}

// Calls a function value with its arguments (moved from).
Value callFunctionValue(const Value& function, Value* args, size_t count, Context& root, const std::string& name) {
    if (!function.isFunction()) notFunction(name, function);
    Object& object = *function.ref();
    const Callee& callee = *object.callee;
    if (callee.lambda) return callLambda(object, args, count, root);
    if (callee.function) return callFunction(*static_cast<const FuncDefNode*>(callee.function.get()), args, count, root);
    return runtime::invoke(*callee.builtin, Arguments(args, count), root);
}

SKRUMI_APART void callValueInPlace(Value* R, int base, int count, Context& root, const std::string& name) {
    Value function = std::move(R[base]); // kept alive while it runs
    Value result = callFunctionValue(function, R + base + 1, static_cast<size_t>(count), root, name);
    R[base] = std::move(result);
}

// value.name(args): R[base] is the value, the arguments follow it.
SKRUMI_APART void callMethod(Value* R, int base, int count, Context& root, const std::string& name) {
    Value self = R[base]; // kept alive while the method runs
    if (self.is(Value::Kind::Struct)) {
        const StructType& type = *self.ref()->structType;
        auto method = type.methods.find(name);
        if (method != type.methods.end()) {
            Value result = callFunction(*static_cast<const FuncDefNode*>(method->second.get()), R + base,
                                        static_cast<size_t>(count) + 1, root);
            R[base] = std::move(result);
            return;
        }
        for (size_t i = 0; i < type.fields.size(); ++i) {
            if (type.fields[i].name != name) continue;
            Value function = self.ref()->items[i];
            Value result = callFunctionValue(function, R + base + 1, static_cast<size_t>(count), root, name);
            R[base] = std::move(result);
            return;
        }
        throw std::runtime_error("Runtime Error: struct '" + type.name + "' has no method '" + name + "'");
    }
    if (self.is(Value::Kind::Map)) {
        long at = self.ref()->find(name);
        if (at < 0 && self.ref()->moduleAlias) {
            // m.sqrt(2): a builtin through a module's alias, as the module's own code calls it.
            if (const runtime::Builtin* builtin = runtime::findBuiltin(name)) {
                Value result = runtime::invoke(*builtin, Arguments(R + base + 1, static_cast<size_t>(count)), root);
                R[base] = std::move(result);
                return;
            }
            throw std::runtime_error("Runtime Error: the module has no function '" + name + "'");
        }
        if (at < 0) throw std::runtime_error("Runtime Error: map has no key '" + name + "' to call");
        Value function = self.ref()->items[static_cast<size_t>(at)];
        Value result = callFunctionValue(function, R + base + 1, static_cast<size_t>(count), root, name);
        R[base] = std::move(result);
        return;
    }
    throw std::runtime_error("Type Error: '." + name + "()' needs a struct or a map, got '" + self.typeName() + "'");
}

SKRUMI_APART void makeClosure(Proto& proto, int index, Value& out, Value* R, Object* current) {
    const Proto& lambda = *proto.lambdas[static_cast<size_t>(index)];
    Value function = Value::container(Value::Kind::Function);
    Object& object = *function.ref();
    object.callee = proto.callees[static_cast<size_t>(index)];
    object.items.reserve(lambda.captures.size());
    for (const auto& capture : lambda.captures)
        object.items.push_back(capture.fromCapture ? current->items[static_cast<size_t>(capture.index)] : R[capture.index]);
    out = std::move(function);
}

SKRUMI_APART void box(Value& slot) {
    Value cell = Value::container(Value::Kind::Box);
    cell.ref()->items.push_back(std::move(slot));
    slot = std::move(cell);
}

SKRUMI_APART void refresh(CallSite& site, Context& root) {
    if (!site.resolved) {
        site.builtin = runtime::findBuiltin(site.name);
        site.resolved = true;
    }
    site.function = static_cast<const FuncDefNode*>(root.getFunc(site.name).get());
    site.root = &root;
    site.generation = root.functionGeneration;
}

// A call of a builtin or of a struct's constructor, or of a Skrumi function that shares
// its name with a builtin.
SKRUMI_APART void callOther(const CallSite& site, Value* args, size_t count, Context& root) {
    // A builtin and a Skrumi function may share a name, like get(items, i) and the
    // server's get(path, handler). A Skrumi declaration always shadows the builtin.
    if (site.builtin && !site.function) {
        Value result = runtime::invoke(*site.builtin, Arguments(args, count), root);
        args[0] = std::move(result);
        return;
    }
    if (site.function) {
        Value result = callFunction(*site.function, args, count, root);
        args[0] = std::move(result);
        return;
    }
    // A global variable that holds a function.
    auto variable = root.variables.find(site.name);
    if (variable != root.variables.end() && variable->second.isFunction()) {
        Value function = variable->second;
        Value result = callFunctionValue(function, args, count, root, site.name);
        args[0] = std::move(result);
        return;
    }
    // A struct's name called like a function builds a value of it.
    if (auto type = root.getStruct(site.name)) {
        Value result = runtime::construct(type, args, count, root);
        args[0] = std::move(result);
        return;
    }
    if (variable != root.variables.end()) notFunction(site.name, variable->second);
    throw std::runtime_error("Runtime Error: Function '" + site.name + "' not found!");
}

inline void call(Proto& proto, const Instr& in, Value* R, Context& root) {
    CallSite& site = proto.calls[static_cast<size_t>(in.b)];
    if (!site.resolved || site.root != &root || site.generation != root.functionGeneration) refresh(site, root);
    Value* args = R + in.a;
    if (site.builtin && !site.function && fastCall(*site.builtin, args, static_cast<size_t>(in.c), args[0])) return;
    if (site.builtin || !site.function) callOther(site, args, static_cast<size_t>(in.c), root);
    else args[0] = callFunction(*site.function, args, static_cast<size_t>(in.c), root);
}

// ---------------------------------------------------------------- the loop

// An error left instruction pc: where the handler that takes it begins, or -1 when no
// handler of this function takes it and the caller must raise it again. It does not
// raise it itself: under MSVC a catch block runs on top of the frames the error left,
// so raising from inside it at every level would stack up until the stack overflows.
SKRUMI_COLD int recover(const Proto& proto, int pc, Value* R, DebugFrame* debug, std::exception_ptr* pending) {
    runtime::StackGuard& guard = runtime::stackGuard();
    std::exception_ptr error = std::current_exception();
    std::string message;
    bool runtimeError = false;
    try {
        throw;
    } catch (const std::exception& e) {
        message = e.what();
        runtimeError = true;
    } catch (...) {
    }
    // The innermost function that sees an error names its line; the functions it
    // passes through on the way out keep that.
    std::size_t messageHash = std::hash<std::string>()(message);
    bool alreadyPlaced = guard.placed && guard.placedMessage == messageHash;
    if (runtimeError && !alreadyPlaced && proto.lines[static_cast<size_t>(pc)] > 0) {
        guard.placed = true;
        guard.placedMessage = messageHash;
        guard.line = proto.lines[static_cast<size_t>(pc)];
        guard.file = proto.file;
    }
    if (debug && runtimeError) debug->hook->error(message, guard.tryDepth > 0);
    const Handler* handler = handlerFor(proto, pc, runtimeError);
    int depth = depthAt(proto, pc);
    if (!handler) {
        guard.tryDepth -= depth;
        if (debug) debug->leaveTo(0);
        return -1;
    }
    if (!handler->finally) guard.placed = false; // caught: the next error is a new one
    guard.tryDepth += depthAt(proto, handler->target) - depth;
    if (debug) debug->leaveTo(static_cast<size_t>(handler->scopes));
    for (int i = handler->clearFrom; i < handler->clearTo; ++i) R[i] = Value();
    if (debug) markDeclared(*debug, handler->clearFrom, handler->clearTo, false);
    if (handler->finally) pending[static_cast<size_t>(handler->pending)] = error;
    else if (handler->message >= 0) R[handler->message] = Value::string(message);
    return handler->target;
}

[[noreturn]] SKRUMI_COLD void raise(const std::string& message) { throw std::runtime_error(message); }

[[noreturn]] SKRUMI_COLD void alreadyDeclared(const std::string& name) {
    throw std::runtime_error("Runtime Error: Variable '" + name + "' is already declared in this scope");
}

// The slow paths of instructions, apart from the loop: each would otherwise keep its
// own temporaries in the loop's native frame, which every Skrumi call pays for.
SKRUMI_APART void binarySlow(runtime::Operator op, const std::string& text, Value& out, const Value& l, const Value& r) {
    out = runtime::binary(op, text, l, r);
}

SKRUMI_APART bool compareSlow(runtime::Operator op, const std::string& text, const Value& l, const Value& r) {
    return runtime::binary(op, text, l, r).asBool();
}

SKRUMI_APART void newArray(Value& out, Value* items, int count) {
    ValueList values;
    values.reserve(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i) values.push_back(std::move(items[i]));
    out = runtime::makeArray(std::move(values));
}

SKRUMI_APART void newMap(Value& out, Value* pairs, int count) {
    Value map = runtime::makeMap();
    map.ref()->keys.reserve(static_cast<size_t>(count));
    map.ref()->items.reserve(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i) map.ref()->slot(pairs[2 * i].str()) = std::move(pairs[2 * i + 1]);
    out = std::move(map);
}

SKRUMI_APART void index(Value& out, Value& base, const Value& key) {
    Value element = runtime::element(base, key, false);
    out = std::move(element);
}

SKRUMI_APART void field(Value& out, Value& base, FieldSite& site) {
    Value member = runtime::member(base, site.name, false);
    if (base.is(Value::Kind::Struct)) {
        const Object& object = *base.ref();
        const auto& fields = object.structType->fields;
        for (size_t i = 0; i < fields.size(); ++i)
            if (fields[i].name == site.name) site.index = i;
        site.type = object.structType.get();
    }
    out = std::move(member);
}

SKRUMI_APART void incrementSlow(Value* out, Value& target, int delta, const std::string& name) {
    Value old = runtime::postIncrement(target, delta, name);
    if (out) *out = std::move(old);
}

SKRUMI_APART void unary(Op op, Value& out, const Value& operand) {
    out = op == Op::Negate ? runtime::negate(operand) : runtime::logicalNot(operand);
}

// A declaration outside any function: a global, or in code compiled for a scope, a
// variable of that scope.
SKRUMI_APART void defineGlobal(const GlobalSite& site, Context& root, Context& scope, Value* value) {
    Context& owner = site.byName ? scope : root;
    if (!value) {
        if (owner.variables.count(site.name) || owner.programGlobals.count(site.name)) alreadyDeclared(site.name);
    } else {
        // `global int x = ...` for a variable of the program's top level sets that one,
        // as it always set the global of that name.
        auto program = owner.programGlobals.find(site.name);
        if (program != owner.programGlobals.end()) {
            *program->second = std::move(*value);
            return;
        }
        owner.variables[site.name] = std::move(*value);
        if (site.declaresConstant) owner.constants.insert(site.name);
        if (!site.nullable.empty()) owner.nullableGlobals[site.name] = site.nullable;
        else if (!owner.nullableGlobals.empty()) owner.nullableGlobals.erase(site.name);
    }
}

[[noreturn]] SKRUMI_COLD void constantChanged(const std::string& name) {
    throw std::runtime_error("Runtime Error: '" + name + "' is a constant and cannot be changed");
}

SKRUMI_APART void setGlobal(GlobalSite& site, Context& root, Context& scope, Value& value) {
    Value& target = *global(site, root, scope);
    Context& owner = site.byName ? scope : root;
    auto nullable = owner.nullableGlobals.find(site.name);
    if (nullable != owner.nullableGlobals.end() && globalVariable(owner, site.name) == &target) {
        runtime::coerce(nullable->second, value, "variable '" + site.name + "'");
        target = std::move(value);
        return;
    }
    runtime::assign(target, std::move(value), site.name);
}

SKRUMI_APART void assignSlow(Value& target, Value& value, const std::string& name) {
    runtime::assign(target, std::move(value), name);
}

SKRUMI_APART void concat(Value& out, const Value* parts, int count) {
    std::string text;
    for (int i = 0; i < count; ++i) runtime::appendDisplay(text, parts[i]);
    out = Value::string(std::move(text));
}

// One round of a for-in loop: false when there is nothing left.
SKRUMI_APART bool forIn(Value* R, const Instr& in) {
    const Value& container = R[in.a];
    Value& position = R[in.a + 1];
    Value& count = R[in.a + 2];
    size_t at = static_cast<size_t>(position.asInt());
    bool pair = in.x == 2;
    Value* first = R + in.b;
    switch (container.kind()) {
        case Value::Kind::Array: {
            const auto& items = container.ref()->items;
            if (at >= items.size()) return false;
            if (pair) {
                first[0].setInt(static_cast<long long>(at));
                first[1] = items[at];
            } else {
                first[0] = items[at];
            }
            break;
        }
        case Value::Kind::Map: {
            const Object& map = *container.ref();
            if (at >= map.keys.size()) return false;
            first[0] = Value::string(map.keys[at]);
            if (pair) first[1] = map.items[at];
            break;
        }
        case Value::Kind::String: {
            const std::string& text = container.str();
            if (at >= text.size()) return false;
            // One character: the bytes of one UTF-8 sequence.
            unsigned char lead = static_cast<unsigned char>(text[at]);
            size_t length = lead < 0x80 ? 1 : (lead >> 5) == 0x6 ? 2 : (lead >> 4) == 0xE ? 3 : (lead >> 3) == 0x1E ? 4 : 1;
            length = std::min(length, text.size() - at);
            Value character = Value::string(text.substr(at, length));
            if (pair) {
                first[0].setInt(count.asInt());
                first[1] = std::move(character);
            } else {
                first[0] = std::move(character);
            }
            position.setInt(static_cast<long long>(at + length));
            count.setInt(count.asInt() + 1);
            return true;
        }
        default:
            throw std::runtime_error("Type Error: a for-in loop needs an array, a map or a string, got '" +
                                     container.typeName() + "'");
    }
    position.setInt(static_cast<long long>(at + 1));
    return true;
}

SKRUMI_APART void mapKey(Value& key) { key = Value::string(runtime::keyOf(key)); }

[[noreturn]] SKRUMI_APART void rethrow(std::exception_ptr& pending) {
    std::exception_ptr error = pending;
    pending = nullptr;
    std::rethrow_exception(error);
}

// GCC and Clang can jump to a label's address; the loop then dispatches each next
// instruction from where the last one ended. Elsewhere it is an ordinary switch.
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic ignored "-Wpedantic" // labels as values are the extension used here
#define SKRUMI_THREADED 1
#define OP(name) case Op::name: L_##name
#define NEXT                                                     \
    do {                                                         \
        SKRUMI_CHECK_CANCEL();                                  \
        in = ip++;                                               \
        goto *jump[static_cast<int>(in->op)];                    \
    } while (0)
#else
#define SKRUMI_THREADED 0
#define OP(name) case Op::name
#define NEXT break
#endif

Value execute(Proto& entry, Value* R, Context& root, Context& entryScope, DebugFrame* debug, Object* closure) {
    // The function running now. A call to a Skrumi function continues in this loop
    // with the callee's code and registers (see OP(Call)); `calls` keeps what to go back to.
#ifdef __ANDROID__
    unsigned androidTicks = 0;
    android::checkpoint();
#endif
    Proto* P = &entry;
    Context* S = &entryScope;
    for (const auto& constant : P->preload) R[constant.first] = P->constants[static_cast<size_t>(constant.second)];
    const Instr* code = P->code.data();
    const Instr* ip = code;
    const Value* K = P->constants.data();
    std::unique_ptr<std::exception_ptr[]> pending;
    if (P->pendingErrors > 0) pending.reset(new std::exception_ptr[static_cast<size_t>(P->pendingErrors)]);
    std::vector<Caller> calls;
    Value returned;
    // Calls run in this loop only without a debugger, which follows native frames.
    bool inlineCalls = !debug && !runtime::debugHook();
    runtime::StackGuard& guard = runtime::stackGuard();
    std::exception_ptr escaping;
    const Instr* in = nullptr;
#if SKRUMI_THREADED
    // Each instruction jumps straight to the code of the next one's operation, instead of
    // going back to one switch: fewer branches, and each one easier to predict.
    static void* const jump[] = {
        &&L_Move,
        &&L_LoadConst,
        &&L_Clear,
        &&L_GetGlobal,
        &&L_SetGlobal,
        &&L_DefineGlobal,
        &&L_Coerce,
        &&L_Assign,
        &&L_Zero,
        &&L_Fail,
        &&L_Add,
        &&L_Sub,
        &&L_Mul,
        &&L_Div,
        &&L_Mod,
        &&L_Eq,
        &&L_Ne,
        &&L_Lt,
        &&L_Le,
        &&L_Gt,
        &&L_Ge,
        &&L_Negate,
        &&L_Not,
        &&L_Truth,
        &&L_Jump,
        &&L_JumpIfFalse,
        &&L_JumpIfTrue,
        &&L_JumpIfNull,
        &&L_JumpIfNotNull,
        &&L_Compare,
        &&L_ForIn,
        &&L_Call,
        &&L_Return,
        &&L_ReturnVoid,
        &&L_NewArray,
        &&L_NewMap,
        &&L_MapKey,
        &&L_Concat,
        &&L_Index,
        &&L_Field,
        &&L_SetPath,
        &&L_Increment,
        &&L_IncrementGlobal,
        &&L_Box,
        &&L_Unbox,
        &&L_BoxStore,
        &&L_BoxAssign,
        &&L_GetCapture,
        &&L_SetCapture,
        &&L_IncrementRef,
        &&L_Closure,
        &&L_CallValue,
        &&L_Method,
        &&L_Declare,
        &&L_Throw,
        &&L_Rethrow,
        &&L_TryEnter,
        &&L_TryLeave,
        &&L_Match,
        &&L_Statement,
        &&L_ScopeEnter,
        &&L_ScopeLeave,
        &&L_Declared,
        &&L_AddInt,
        &&L_SubInt,
        &&L_MulInt,
        &&L_DivInt,
        &&L_ModInt,
        &&L_CompareInt,
        &&L_CallDirect,
        &&L_AddIntK,
        &&L_SubIntK,
        &&L_CompareIntK,
    };
#endif

    for (;;) {
        try {
            for (;;) {
                SKRUMI_CHECK_CANCEL();
                in = ip++;
                switch (in->op) {
                    OP(Move):
                        if (in->x) R[in->a] = std::move(R[in->b]);
                        else R[in->a] = R[in->b];
                        NEXT;
                    OP(LoadConst):
                        R[in->a] = K[in->b];
                        NEXT;
                    OP(Clear):
                        for (int i = in->a; i < in->b; ++i) R[i] = Value();
                        if (debug) markDeclared(*debug, in->a, in->b, false);
                        NEXT;
                    OP(GetGlobal):
                        R[in->a] = *readGlobal(P->globals[static_cast<size_t>(in->b)], root, *S);
                        NEXT;
                    OP(SetGlobal): {
                        GlobalSite& site = P->globals[static_cast<size_t>(in->b)];
                        Value& target = *global(site, root, *S);
                        if (site.constant) constantChanged(site.name);
                        Value& value = R[in->a];
                        if (target.kind() == value.kind() && !target.is(Value::Kind::Struct) &&
                            (!value.isInt() || fitsInt(value.asInt())))
                            target = std::move(value);
                        else setGlobal(site, root, *S, value);
                        NEXT;
                    }
                    OP(DefineGlobal):
                        defineGlobal(P->globals[static_cast<size_t>(in->b)], root, *S, in->a < 0 ? nullptr : &R[in->a]);
                        NEXT;
                    OP(Coerce): {
                        const Conversion& conversion = P->conversions[static_cast<size_t>(in->b)];
                        if (!runtime::storesAsIs(conversion.kind, R[in->a])) runtime::coerce(conversion.type, R[in->a], conversion.what);
                        NEXT;
                    }
                    OP(Assign): {
                        Value& target = R[in->a];
                        Value& value = R[in->b];
                        // An int beyond int's range (a big literal) converts, with its error.
                        if (target.kind() == value.kind() && !target.is(Value::Kind::Struct) &&
                            (!value.isInt() || fitsInt(value.asInt())))
                            target = std::move(value);
                        else assignSlow(target, value, K[in->c].str());
                        NEXT;
                    }
                    OP(Zero):
                        R[in->a] = runtime::zeroValue(K[in->b].str(), root);
                        NEXT;
                    OP(Fail):
                        raise(K[in->a].str());

                    OP(Add): {
                        const Value& l = R[in->b];
                        const Value& r = R[in->c];
                        if (l.isInt() && r.isInt()) {
                            long long sum = l.asInt() + r.asInt();
                            if (fitsInt(sum)) {
                                R[in->a].setInt(sum);
                                NEXT;
                            }
                        }
                        binarySlow(runtime::Operator::Add, P->texts[in->y], R[in->a], l, r);
                        NEXT;
                    }
                    OP(Sub): {
                        const Value& l = R[in->b];
                        const Value& r = R[in->c];
                        if (l.isInt() && r.isInt()) {
                            long long difference = l.asInt() - r.asInt();
                            if (fitsInt(difference)) {
                                R[in->a].setInt(difference);
                                NEXT;
                            }
                        }
                        binarySlow(runtime::Operator::Sub, P->texts[in->y], R[in->a], l, r);
                        NEXT;
                    }
                    OP(Mul): {
                        const Value& l = R[in->b];
                        const Value& r = R[in->c];
                        if (l.isInt() && r.isInt()) {
                            long long product = l.asInt() * r.asInt();
                            if (fitsInt(l.asInt()) && fitsInt(r.asInt()) && fitsInt(product)) {
                                R[in->a].setInt(product);
                                NEXT;
                            }
                        }
                        binarySlow(runtime::Operator::Mul, P->texts[in->y], R[in->a], l, r);
                        NEXT;
                    }
                    OP(Div):
                    OP(Mod): {
                        const Value& l = R[in->b];
                        const Value& r = R[in->c];
                        if (l.isInt() && r.isInt() && r.asInt() != 0 && fitsInt(l.asInt()) && fitsInt(r.asInt())) {
                            // Both fit 32 bits (checked above): a 32-bit division is several times faster.
                            auto a32 = static_cast<std::int32_t>(l.asInt()), b32 = static_cast<std::int32_t>(r.asInt());
                            long long result = b32 == -1 ? (in->op == Op::Div ? -static_cast<long long>(a32) : 0)
                                                         : (in->op == Op::Div ? a32 / b32 : a32 % b32);
                            if (fitsInt(result)) {
                                R[in->a].setInt(result);
                                NEXT;
                            }
                        }
                        binarySlow(in->op == Op::Div ? runtime::Operator::Div : runtime::Operator::Mod, P->texts[in->y],
                                   R[in->a], l, r);
                        NEXT;
                    }
                    OP(Eq): OP(Ne): OP(Lt): OP(Le): OP(Gt): OP(Ge): {
                        const Value& l = R[in->b];
                        const Value& r = R[in->c];
                        auto op = static_cast<runtime::Operator>(static_cast<int>(runtime::Operator::Eq) +
                                                                 (static_cast<int>(in->op) - static_cast<int>(Op::Eq)));
                        if (l.isInt() && r.isInt()) {
                            long long a = l.asInt(), b = r.asInt();
                            bool result = op == runtime::Operator::Eq ? a == b : op == runtime::Operator::Ne ? a != b
                                        : op == runtime::Operator::Lt ? a < b : op == runtime::Operator::Le ? a <= b
                                        : op == runtime::Operator::Gt ? a > b : a >= b;
                            R[in->a].setBool(result);
                            NEXT;
                        }
                        binarySlow(op, P->texts[in->y], R[in->a], l, r);
                        NEXT;
                    }
                    OP(Negate):
                    OP(Not):
                        unary(in->op, R[in->a], R[in->b]);
                        NEXT;
                    OP(Truth):
                        runtime::operandTruth(R[in->a], in->x ? "right" : "left", P->texts[in->y]);
                        NEXT;

                    OP(Jump):
                        ip = code + in->a;
                        NEXT;
                    OP(JumpIfFalse):
                        if (!truth(R[in->a], in->x)) ip = code + in->b;
                        NEXT;
                    OP(JumpIfTrue):
                        if (truth(R[in->a], in->x)) ip = code + in->b;
                        NEXT;
                    OP(JumpIfNull):
                        if (R[in->a].isVoid()) ip = code + in->b;
                        NEXT;
                    OP(JumpIfNotNull):
                        if (!R[in->a].isVoid()) ip = code + in->b;
                        NEXT;
                    OP(Compare): {
                        const Value& l = R[in->a];
                        const Value& r = R[in->b];
                        auto op = static_cast<runtime::Operator>(in->x);
                        bool result;
                        if (l.isInt() && r.isInt()) {
                            long long a = l.asInt(), b = r.asInt();
                            switch (op) {
                                case runtime::Operator::Lt: result = a < b; break;
                                case runtime::Operator::Le: result = a <= b; break;
                                case runtime::Operator::Gt: result = a > b; break;
                                case runtime::Operator::Ge: result = a >= b; break;
                                case runtime::Operator::Eq: result = a == b; break;
                                default: result = a != b; break;
                            }
                        } else {
                            result = compareSlow(op, P->texts[in->y >> 1], l, r);
                        }
                        if (result == ((in->y & 1) != 0)) ip = code + in->c;
                        NEXT;
                    }

                    OP(ForIn):
                        if (!forIn(R, *in)) ip = code + in->c;
                        NEXT;
                    OP(CallDirect): {
                        CallSite& site = P->calls[static_cast<size_t>(in->b)];
                        if (!site.resolved || site.root != &root || site.generation != root.functionGeneration) refresh(site, root);
                        const std::int32_t* registersOfArgs = P->argRegisters.data() + in->y;
                        size_t count = static_cast<size_t>(in->c);
                        if (site.builtin && !site.function) {
                            if (site.builtin == builtinPush && count == 2 && R[registersOfArgs[0]].is(Value::Kind::Array)) {
                                Object& array = *R[registersOfArgs[0]].ref();
                                if (!array.elementType && array.items.size() < (size_t{1} << 26)) {
                                    array.items.push_back(R[registersOfArgs[1]]);
                                    R[in->a].reset();
                                    NEXT;
                                }
                            } else if (site.builtin == builtinSize && count == 1 && R[registersOfArgs[0]].is(Value::Kind::Array)) {
                                R[in->a].setInt(static_cast<long long>(R[registersOfArgs[0]].ref()->items.size()));
                                NEXT;
                            }
                            if (site.builtin->fast && fastDirect(*site.builtin, R, registersOfArgs, count, R[in->a])) NEXT;
                        }
                        if (!site.builtin && site.function && inlineCalls) {
                            Proto& callee = const_cast<Proto&>(protoOf(*site.function, false));
                            if (callee.forward >= 0 && forwardDirect(callee, R, registersOfArgs, count, R[in->a], root)) NEXT;
                            if (callee.forward < 0) {
                                R = enterCall(calls, callee, *in, P, ip, R, closure, S, pending, guard, registersOfArgs);
                                P = &callee;
                                closure = nullptr;
                                S = &root;
                                code = P->code.data();
                                K = P->constants.data();
                                ip = code;
                                NEXT;
                            }
                        }
                        // Any other callee takes its arguments in a row, as Call has them.
                        for (size_t i = 0; i < count; ++i) R[in->a + static_cast<int>(i)] = R[registersOfArgs[i]];
                        goto ordinaryCall;
                    }
                    OP(Call):
                    ordinaryCall: {
                        CallSite& site = P->calls[static_cast<size_t>(in->b)];
                        if (!site.resolved || site.root != &root || site.generation != root.functionGeneration) refresh(site, root);
                        if (site.builtin) {
                            Value* args = R + in->a;
                            size_t count = static_cast<size_t>(in->c);
                            if (!site.function && fastCall(*site.builtin, args, count, args[0])) NEXT;
                            callOther(site, args, count, root);
                            NEXT;
                        }
                        if (!site.function || !inlineCalls) {
                            call(*P, *in, R, root);
                            NEXT;
                        }
                        Proto& callee = const_cast<Proto&>(protoOf(*site.function, false));
                        if (callee.forward >= 0) {
                            call(*P, *in, R, root);
                            NEXT;
                        }
                        // A Skrumi function: its frame opens here, without a native call.
                        R = enterCall(calls, callee, *in, P, ip, R, closure, S, pending, guard);
                        P = &callee;
                        closure = nullptr;
                        S = &root;
                        code = P->code.data();
                        K = P->constants.data();
                        ip = code;
                        NEXT;
                    }
                    OP(Return):
                        if (calls.empty()) return std::move(R[in->a]);
                        returned = std::move(R[in->a]);
                        goto leave;
                    OP(ReturnVoid):
                        if (calls.empty()) return Value();
                        returned.reset();
                        goto leave;

                    OP(NewArray):
                        newArray(R[in->a], R + in->b, in->c);
                        NEXT;
                    OP(NewMap):
                        newMap(R[in->a], R + in->b, in->c);
                        NEXT;
                    OP(Concat):
                        concat(R[in->a], R + in->b, in->c);
                        NEXT;
                    OP(MapKey):
                        if (!R[in->a].isString()) mapKey(R[in->a]);
                        NEXT;
                    OP(Index): {
                        // An array element by an int in range is the common case.
                        const Value& base = R[in->b];
                        const Value& key = R[in->c];
                        if (base.is(Value::Kind::Array) && key.isInt() && in->a != in->b) {
                            const auto& items = base.ref()->items;
                            long long at = key.asInt();
                            if (at >= 0 && static_cast<unsigned long long>(at) < items.size()) {
                                R[in->a] = items[static_cast<size_t>(at)];
                                NEXT;
                            }
                        }
                        index(R[in->a], R[in->b], R[in->c]);
                        NEXT;
                    }
                    OP(Field): {
                        FieldSite& site = P->fields[static_cast<size_t>(in->c)];
                        const Value& base = R[in->b];
                        if (base.is(Value::Kind::Struct) && base.ref()->structType.get() == site.type && in->a != in->b) {
                            R[in->a] = base.ref()->items[site.index];
                            NEXT;
                        }
                        field(R[in->a], R[in->b], site);
                        NEXT;
                    }
                    OP(SetPath):
                        setPath(*P, P->paths[static_cast<size_t>(in->b)], R, R[in->a], root, *S, closure);
                        NEXT;
                    OP(Increment): {
                        Value& target = R[in->b];
                        int delta = (in->c & 1) ? 1 : -1;
                        if (target.isInt() && fitsInt(target.asInt()) && fitsInt(target.asInt() + delta)) {
                            long long old = target.asInt();
                            target.setInt(old + delta);
                            if (in->a >= 0) R[in->a].setInt(old);
                            NEXT;
                        }
                        incrementSlow(in->a >= 0 ? &R[in->a] : nullptr, target, delta, K[in->c >> 1].str());
                        NEXT;
                    }
                    OP(IncrementGlobal): {
                        GlobalSite& site = P->globals[static_cast<size_t>(in->b)];
                        Value& target = *global(site, root, *S);
                        if (site.constant) constantChanged(site.name);
                        int delta = in->c ? 1 : -1;
                        if (target.isInt() && fitsInt(target.asInt() + delta)) {
                            long long old = target.asInt();
                            target.setInt(old + delta);
                            if (in->a >= 0) R[in->a].setInt(old);
                            NEXT;
                        }
                        incrementSlow(in->a >= 0 ? &R[in->a] : nullptr, target, delta, site.name);
                        NEXT;
                    }

                    OP(Box):
                        box(R[in->a]);
                        NEXT;
                    OP(Unbox):
                        R[in->a] = R[in->b].ref()->items[0];
                        NEXT;
                    OP(BoxStore):
                        R[in->a].ref()->items[0] = std::move(R[in->b]);
                        NEXT;
                    OP(BoxAssign): {
                        Value& target = R[in->a].ref()->items[0];
                        Value& value = R[in->b];
                        // An int beyond int's range (a big literal) converts, with its error.
                        if (target.kind() == value.kind() && !target.is(Value::Kind::Struct) &&
                            (!value.isInt() || fitsInt(value.asInt())))
                            target = std::move(value);
                        else assignSlow(target, value, K[in->c].str());
                        NEXT;
                    }
                    OP(GetCapture):
                        R[in->a] = closure->items[static_cast<size_t>(in->b)].ref()->items[0];
                        NEXT;
                    OP(SetCapture): {
                        Value& target = closure->items[static_cast<size_t>(in->a)].ref()->items[0];
                        Value& value = R[in->b];
                        if (in->x || (target.kind() == value.kind() && !target.is(Value::Kind::Struct))) target = std::move(value);
                        else assignSlow(target, value, K[in->c].str());
                        NEXT;
                    }
                    OP(IncrementRef): {
                        Value& target = in->x ? closure->items[static_cast<size_t>(in->b)].ref()->items[0] : R[in->b].ref()->items[0];
                        incrementSlow(in->a >= 0 ? &R[in->a] : nullptr, target, (in->c & 1) ? 1 : -1, K[in->c >> 1].str());
                        NEXT;
                    }
                    OP(Closure):
                        makeClosure(*P, in->b, R[in->a], R, closure);
                        NEXT;
                    OP(CallValue):
                        callValueInPlace(R, in->a, in->c, root, K[in->b].str());
                        NEXT;
                    OP(Method):
                        callMethod(R, in->a, in->c, root, K[in->b].str());
                        NEXT;
                    OP(Declare): {
                        Declaration* declaration = P->declarations[static_cast<size_t>(in->b)];
                        if (!in->c || !declaration->exists(root)) declaration->declare(root);
                        NEXT;
                    }
                    OP(Throw):
                        raise(runtime::display(R[in->a]));
                    OP(Rethrow):
                        rethrow(pending[static_cast<size_t>(in->a)]);
                    OP(TryEnter):
                        ++guard.tryDepth;
                        NEXT;
                    OP(TryLeave):
                        --guard.tryDepth;
                        NEXT;
                    OP(Match):
                        R[in->a].setBool(runtime::switchMatches(R[in->b], R[in->c]));
                        NEXT;

                    OP(Statement):
                        guard.line = in->a;
                        guard.file = P->file;
                        if (debug) debug->hook->statement(P->file, in->a);
                        NEXT;
                    OP(ScopeEnter):
                        if (debug) debug->enter(in->a != 0);
                        NEXT;
                    OP(ScopeLeave):
                        if (debug) debug->leave();
                        NEXT;
                    OP(Declared):
                        if (in->b) declareProgramGlobal(P->globals[static_cast<size_t>(in->b - 1)], R[in->a], root);
                        if (debug) markDeclared(*debug, in->a, in->a + 1, true);
                        NEXT;

                    // Both operands are ints in range, so the sums and products fit
                    // 64 bits; only the result can leave int's range.
                    OP(AddInt): {
                        long long result = R[in->b].asInt() + R[in->c].asInt();
                        if (!fitsInt(result)) binarySlow(runtime::Operator::Add, P->texts[in->y], R[in->a], R[in->b], R[in->c]);
                        else R[in->a].setInt(result);
                        NEXT;
                    }
                    OP(SubInt): {
                        long long result = R[in->b].asInt() - R[in->c].asInt();
                        if (!fitsInt(result)) binarySlow(runtime::Operator::Sub, P->texts[in->y], R[in->a], R[in->b], R[in->c]);
                        else R[in->a].setInt(result);
                        NEXT;
                    }
                    OP(MulInt): {
                        long long result = R[in->b].asInt() * R[in->c].asInt();
                        if (!fitsInt(result)) binarySlow(runtime::Operator::Mul, P->texts[in->y], R[in->a], R[in->b], R[in->c]);
                        else R[in->a].setInt(result);
                        NEXT;
                    }
                    OP(DivInt):
                    OP(ModInt): {
                        long long l = R[in->b].asInt(), r = R[in->c].asInt();
                        // Division by zero and -2147483648 / -1 take the checked path.
                        if (r == 0 || (r == -1 && l == -2147483648LL)) {
                            binarySlow(in->op == Op::DivInt ? runtime::Operator::Div : runtime::Operator::Mod, P->texts[in->y],
                                       R[in->a], R[in->b], R[in->c]);
                        } else {
                            // ints are 32-bit: a 32-bit division is several times faster.
                            auto a32 = static_cast<std::int32_t>(l), b32 = static_cast<std::int32_t>(r);
                            R[in->a].setInt(in->op == Op::DivInt ? a32 / b32 : a32 % b32);
                        }
                        NEXT;
                    }
                    OP(AddIntK): {
                        long long result = R[in->b].asInt() + in->c;
                        if (!fitsInt(result)) binarySlow(runtime::Operator::Add, P->texts[in->y], R[in->a], R[in->b], Value::integer(in->c));
                        else R[in->a].setInt(result);
                        NEXT;
                    }
                    OP(SubIntK): {
                        long long result = R[in->b].asInt() - in->c;
                        if (!fitsInt(result)) binarySlow(runtime::Operator::Sub, P->texts[in->y], R[in->a], R[in->b], Value::integer(in->c));
                        else R[in->a].setInt(result);
                        NEXT;
                    }
                    OP(CompareIntK): {
                        long long a = R[in->a].asInt(), b = in->b;
                        // Which of <, ==, > the operator accepts, as bits 0 to 2 (see compareMask).
                        int order = (a > b) - (a < b) + 1;
                        bool result = (compareMask[in->x] >> order) & 1;
                        if (result == ((in->y & 1) != 0)) ip = code + in->c;
                        NEXT;
                    }
                    OP(CompareInt): {
                        long long a = R[in->a].asInt(), b = R[in->b].asInt();
                        bool result;
                        switch (static_cast<runtime::Operator>(in->x)) {
                            case runtime::Operator::Lt: result = a < b; break;
                            case runtime::Operator::Le: result = a <= b; break;
                            case runtime::Operator::Gt: result = a > b; break;
                            case runtime::Operator::Ge: result = a >= b; break;
                            case runtime::Operator::Eq: result = a == b; break;
                            default: result = a != b; break;
                        }
                        if (result == ((in->y & 1) != 0)) ip = code + in->c;
                        NEXT;
                    }
                }
                continue;
            leave: {
                // Back to the caller of a call this loop ran: the callee's registers go,
                // and its result is converted as its declaration says, in the caller, so
                // that an error there names the call's line.
                Caller& caller = calls.back();
                Proto& finished = *P;
                leaveRegisters(caller.callee);
                --guard.vmDepth;
                P = caller.proto;
                R = caller.R;
                ip = caller.ip;
                closure = caller.closure;
                S = caller.scope;
                pending.reset(caller.pending);
                int into = caller.result;
                calls.pop_back();
                code = P->code.data();
                K = P->constants.data();
                if (finished.result.kind == Value::Kind::Void) {
                    returned.reset();
                } else if (returned.isVoid()) {
                    if (!isNullable(finished.result.type)) noResult(finished);
                } else if (!runtime::storesAsIs(finished.result.kind, returned)) {
                    convert(finished.result, returned);
                }
                R[into] = std::move(returned);
            }
            }
        } catch (...) {
            // The error leaves functions until one of them handles it; a call this loop
            // runs is left here, the rest by raising it again to the native caller.
            int target = recover(*P, static_cast<int>(ip - code) - 1, R, debug, pending.get());
            while (target < 0 && !calls.empty()) {
                Caller& caller = calls.back();
                leaveRegisters(caller.callee);
                --guard.vmDepth;
                P = caller.proto;
                R = caller.R;
                ip = caller.ip;
                closure = caller.closure;
                S = caller.scope;
                pending.reset(caller.pending);
                calls.pop_back();
                code = P->code.data();
                K = P->constants.data();
                target = recover(*P, static_cast<int>(ip - code) - 1, R, debug, pending.get());
            }
            if (target >= 0) {
                ip = code + target;
                continue;
            }
            escaping = std::current_exception();
        }
        // Raised again only now, outside the catch block, once this frame's handling is over.
        std::rethrow_exception(escaping);
    }
}

} // namespace

Value call(const FuncDefNode& function, Value* args, size_t count, Context& caller) {
    return callFunction(function, args, count, *caller.getRoot());
}

void run(BlockNode& program, Context& root, Unit unit) {
    DebugHook* hook = unit == Unit::Program ? runtime::debugHook() : nullptr;
    std::shared_ptr<Proto> proto = compileProgram(program, unit, hook != nullptr);
    Window window(static_cast<size_t>(proto->registers));
    // The top level's registers become globals when it ends: none may hold an old value.
    for (int i = 0; i < proto->registers; ++i) window.base()[i].reset();
    if (unit != Unit::Program) {
        execute(*proto, window.base(), root, root, nullptr);
        return;
    }
    // The blocks of the program keep their variables in numbered slots, alive while it
    // runs; the debugger and its console find them through the root scope.
    struct Frame {
        Context& root;
        Value* slots;
        const std::vector<std::string>* names;
        bool frame;
        ~Frame() {
            // The top level's variables outlive its registers as ordinary globals, for
            // code that runs later: another runSource, a callback, Interpreter::getGlobal.
            for (const auto& variable : root.programGlobals) root.variables.emplace(variable.first, *variable.second);
            if (!root.programGlobals.empty()) {
                root.programGlobals.clear();
                ++root.generation; // lookups cached the registers
            }
            root.slots = slots;
            root.slotNames = names;
            root.frame = frame;
        }
    } restore{root, root.slots, root.slotNames, root.frame};
    root.slots = window.base();
    root.slotNames = proto->slotNames.get();
    root.frame = true;
    if (!hook) {
        execute(*proto, window.base(), root, root, nullptr);
        return;
    }
    std::vector<char> outerDeclared(static_cast<size_t>(proto->slots), 0);
    root.declared.swap(outerDeclared);
    DebugFrame frame{hook, &root, {}, {}};
    struct Leave {
        DebugFrame& frame;
        std::vector<char>& outer;
        ~Leave() {
            frame.leaveTo(0);
            frame.function->declared.swap(outer);
        }
    } leave{frame, outerDeclared};
    execute(*proto, window.base(), root, root, &frame);
}

Value run(Proto& proto, Context& scope) {
    Window window(static_cast<size_t>(proto.registers));
    return execute(proto, window.base(), *scope.getRoot(), scope, nullptr);
}

Value evaluate(Node& expression, Context& scope) {
    return run(*compileExpression(expression), scope);
}

void execute(BlockNode& statements, Context& scope, const std::string& outside) {
    run(*compileStatements(statements, outside), scope);
}

} // namespace skrumi::vm

namespace skrumi::runtime {

Value callValue(const Value& function, Arguments args, Context& ctx) {
    ValueList values(args.begin(), args.end());
    return vm::callFunctionValue(function, values.data(), values.size(), *ctx.getRoot(), "function");
}

Value callValue(const Value& function, Value* args, size_t count, Context& ctx) {
    // A call from outside any Skrumi code (an HTTP handler) starts with no error placed.
    if (stackGuard().depth == 0) stackGuard().placed = false;
    return vm::callFunctionValue(function, args, count, *ctx.getRoot(), "function");
}

} // namespace skrumi::runtime
