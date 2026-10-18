// Compiles the parser's tree into bytecode for the VM (Vm.cpp), one function or one
// program top level at a time. The resolver has already numbered the local variables;
// they become the first registers of the frame. Everything a statement computes on
// the way lives in temporaries above them, freed when the statement ends.
#include "skrumi/Bytecode.h"
#include "skrumi/AST.h"
#include "skrumi/Resolver.h"
#include <algorithm>
#include <cstring>
#include <map>
#include <stdexcept>

namespace skrumi::bytecode {
namespace {

// Constant registers are numbered from here while compiling, and placed after the
// temporaries once their count is known.
constexpr int constantBase = 1 << 24;

// Where inside a try statement code is: its body, its catch block, a copy of its
// finally block run on the way out, or the finally block run for an error.
enum class Part { Body, Handler, Finally, Rescue };

bool isDeclaration(const Node* node) {
    return dynamic_cast<const FuncDefNode*>(node) || dynamic_cast<const VarDeclNode*>(node) ||
           dynamic_cast<const ArrayDeclNode*>(node) || dynamic_cast<const UsingNode*>(node) ||
           dynamic_cast<const IncludeNode*>(node) || dynamic_cast<const StructDefNode*>(node) ||
           dynamic_cast<const EnumDefNode*>(node);
}

bool isComparison(runtime::Operator op) {
    return op >= runtime::Operator::Eq && op <= runtime::Operator::Ge;
}

// Whether evaluating the expression can change a local variable: only name++ can.
bool mutates(const Node* node) {
    if (!node) return false;
    if (dynamic_cast<const PostIncNode*>(node)) return true;
    if (auto* n = dynamic_cast<const BinOpNode*>(node)) return mutates(n->left.get()) || mutates(n->right.get());
    if (auto* n = dynamic_cast<const UnaryOpNode*>(node)) return mutates(n->operand.get());
    if (auto* n = dynamic_cast<const IndexNode*>(node)) return mutates(n->base.get()) || mutates(n->index.get());
    if (auto* n = dynamic_cast<const FieldNode*>(node)) return mutates(n->base.get());
    if (auto* n = dynamic_cast<const FuncCallNode*>(node)) {
        for (const auto& arg : n->args)
            if (mutates(arg.get())) return true;
        return false;
    }
    if (auto* n = dynamic_cast<const ArrayLiteralNode*>(node)) {
        for (const auto& element : n->elements)
            if (mutates(element.get())) return true;
        return false;
    }
    if (auto* n = dynamic_cast<const MapLiteralNode*>(node)) {
        for (const auto& entry : n->entries)
            if (mutates(entry.first.get()) || mutates(entry.second.get())) return true;
        return false;
    }
    if (auto* n = dynamic_cast<const InterpolationNode*>(node)) {
        for (const auto& part : n->parts)
            if (mutates(part.get())) return true;
        return false;
    }
    return false;
}

class Compiler {
public:
    // In code compiled for a scope, names that are not local are found through the scope,
    // and `return`, `break` and `continue` outside a loop raise `outside`.
    bool names = false;
    std::string outside;
    const std::vector<bool>* boxedSlots = nullptr; // the frame's slots that lambdas capture
    const std::vector<std::string>* slotTypes = nullptr; // the declared type of each slot
    const std::vector<bool>* slotConstants = nullptr;    // the slots declared const

    Compiler(Proto& proto, bool debug, bool function) : p(proto), debug(debug), function(function) {
        next = p.slots;
        p.registers = p.slots;
    }

    void block(BlockNode& block, bool loopBody = false, bool functionBody = false) {
        int outerLine = line;
        if (debug) {
            emit(Op::ScopeEnter, block.scoped ? 1 : 0);
            ++scopes;
        }
        for (auto& stmt : block.stmts) {
            if (!stmt) continue;
            if (stmt->range.start.line > 0) line = stmt->range.start.line;
            // A function definition only registers the function; stepping over it is noise.
            if (debug && stmt->range.start.line > 0 && !dynamic_cast<const FuncDefNode*>(stmt.get()))
                emit(Op::Statement, line);
            statement(*stmt);
        }
        if (debug) {
            emit(Op::ScopeLeave);
            --scopes;
        }
        // The block's variables end with it. A loop body's are overwritten by the next
        // round and cleared when the loop ends, so only a debugger needs them cleared here.
        if (!functionBody && block.endSlot > block.firstSlot && (debug || !loopBody))
            emit(Op::Clear, block.firstSlot, block.endSlot);
        line = outerLine;
    }

    // The functions and struct types of a top level exist from its start, so code may
    // call a function defined further down. A name that already exists keeps its
    // meaning until the definition itself runs.
    void hoist(BlockNode& program) {
        int outerLine = line;
        for (auto& stmt : program.stmts) {
            auto* declaration = dynamic_cast<Declaration*>(stmt.get());
            if (!declaration || !(dynamic_cast<FuncDefNode*>(declaration) || dynamic_cast<StructDefNode*>(declaration) ||
                                  dynamic_cast<EnumDefNode*>(declaration)))
                continue;
            line = stmt->range.start.line;
            p.declarations.push_back(declaration);
            emit(Op::Declare, 0, static_cast<int>(p.declarations.size()) - 1, 1);
        }
        line = outerLine;
    }

    void topLevel(BlockNode& program, Unit unit) {
        hoist(program);
        for (auto& stmt : program.stmts) {
            if (!stmt || (unit == Unit::Declarations && !isDeclaration(stmt.get()))) continue;
            if (stmt->range.start.line > 0) line = stmt->range.start.line;
            statement(*stmt);
        }
    }

    void statement(Node& node) {
        int mark = next;
        if (auto* declaration = dynamic_cast<Declaration*>(&node)) {
            p.declarations.push_back(declaration);
            emit(Op::Declare, 0, static_cast<int>(p.declarations.size()) - 1);
        } else if (auto* n = dynamic_cast<VarDeclNode*>(&node)) {
            variable(*n);
        } else if (auto* n = dynamic_cast<ArrayDeclNode*>(&node)) {
            arrayVariable(*n);
        } else if (auto* n = dynamic_cast<VarAssignNode*>(&node)) {
            assignment(*n);
        } else if (auto* n = dynamic_cast<SetNode*>(&node)) {
            setPath(*n);
        } else if (auto* n = dynamic_cast<PostIncNode*>(&node)) {
            increment(*n, -1);
        } else if (auto* n = dynamic_cast<ReturnNode*>(&node)) {
            returnStatement(*n);
        } else if (dynamic_cast<BreakNode*>(&node)) {
            jumpOut(false);
        } else if (dynamic_cast<ContinueNode*>(&node)) {
            jumpOut(true);
        } else if (auto* n = dynamic_cast<IfNode*>(&node)) {
            ifStatement(*n);
        } else if (auto* n = dynamic_cast<WhileNode*>(&node)) {
            whileLoop(*n);
        } else if (auto* n = dynamic_cast<ForInNode*>(&node)) {
            forIn(*n);
        } else if (auto* n = dynamic_cast<ForNode*>(&node)) {
            forLoop(*n);
        } else if (auto* n = dynamic_cast<SwitchNode*>(&node)) {
            switchStatement(*n);
        } else if (auto* n = dynamic_cast<TryNode*>(&node)) {
            tryStatement(*n);
        } else if (auto* n = dynamic_cast<ThrowNode*>(&node)) {
            emit(Op::Throw, any(*n->message));
        } else if (auto* n = dynamic_cast<BlockNode*>(&node)) {
            block(*n);
        } else {
            any(node); // an expression statement, such as a call
        }
        next = mark;
    }

    // Constant registers go after the temporaries, now that their number is known.
    void finish() {
        int base = p.registers;
        auto place = [&](std::int32_t& field) {
            if (field >= constantBase) field = base + (field - constantBase);
        };
        for (auto& instr : p.code) {
            place(instr.a);
            // An int written into the instruction is no register.
            if (instr.op != Op::CompareIntK) place(instr.b);
            if (instr.op != Op::AddIntK && instr.op != Op::SubIntK) place(instr.c);
        }
        for (auto& path : p.paths)
            for (auto& step : path.steps) place(step.key);
        for (auto& argument : p.argRegisters) place(argument);
        for (size_t i = 0; i < constantOrder.size(); ++i)
            p.preload.push_back({base + static_cast<int>(i), constantOrder[i]});
        p.registers = base + static_cast<int>(constantOrder.size());
    }

    // Parameters a lambda captures go into boxes before the body runs.
    void boxParameters(size_t count) {
        for (size_t i = 0; i < count; ++i)
            if (boxed(static_cast<int>(i))) emit(Op::Box, static_cast<int>(i));
    }

    // A lambda written in this code: compiled with it, created each time the code reaches it.
    std::shared_ptr<Proto> compileLambda(LambdaNode& node) {
        auto proto = std::make_shared<Proto>();
        proto->name = "lambda";
        proto->lambda = true;
        proto->file = node.body->file ? node.body->file : p.file;
        proto->line = node.range.start.line;
        proto->debug = debug;
        proto->slotNames = std::make_shared<std::vector<std::string>>(node.layout->names);
        proto->slots = static_cast<int>(node.layout->names.size());
        for (const auto& param : node.params) {
            if (param.type.empty()) proto->params.push_back({Value::Kind::Void, "", ""});
            else proto->params.push_back({runtime::declaredKind(param.type), param.type, "parameter '" + param.name + "' of a lambda"});
        }
        proto->captures = node.captures;
        Compiler inner(*proto, debug, true);
        inner.boxedSlots = &node.layout->boxed;
        inner.slotTypes = &node.layout->types;
        inner.slotConstants = &node.layout->constants;
        inner.line = node.range.start.line;
        inner.boxParameters(node.params.size());
        inner.block(*node.body, false, true);
        inner.end(node.body->range.end.line);
        inner.epilogue();
        inner.finish();
        return proto;
    }

    // An expression's code: its value is what the code returns. It names no line of its
    // own (it does not know its file), so an error in it is placed at the code that ran it.
    void value(Node& expression) {
        line = 0;
        int reg = temp();
        into(expression, reg);
        emit(Op::Return, reg);
    }

    // The implicit return at the end of the code belongs to its last line.
    void end(int endLine) {
        if (endLine > 0) line = endLine;
        emit(Op::ReturnVoid);
    }

    int emit(Op op, int a = 0, int b = 0, int c = 0, int x = 0, int y = 0) {
        Instr instr;
        instr.op = op;
        instr.x = static_cast<std::uint8_t>(x);
        instr.y = static_cast<std::uint16_t>(y);
        instr.a = a;
        instr.b = b;
        instr.c = c;
        p.code.push_back(instr);
        p.lines.push_back(line);
        return static_cast<int>(p.code.size()) - 1;
    }

private:
    Proto& p;
    bool debug;
    bool function; // a function body; otherwise a program's or a module's top level
    int next = 0;  // the first free temporary
    int line = 0;
    int scopes = 0; // blocks open, for the debugger's scope events

    struct Target {
        bool loop;
        std::vector<int> breaks, continues;
        size_t tries;
        int scopes;
        int clearFrom = 0, clearTo = 0; // a loop body's variables, cleared by continue
    };
    std::vector<Target> targets;

    struct TryContext {
        TryNode* node;
        Part part;
        int pending;
    };
    std::vector<TryContext> tries;
    int tryLevel = 0;

    std::map<long long, int> ints;
    std::map<std::string, int> strings;
    std::map<std::uint64_t, int> reals;
    int bools[2] = {-1, -1};
    std::map<int, int> constantRegisters;
    std::vector<int> constantOrder;
    std::map<std::string, int> globalSites, callSites, textIndex;

    int here() const { return static_cast<int>(p.code.size()); }

    void patch(int at, int target) {
        Instr& instr = p.code[static_cast<size_t>(at)];
        switch (instr.op) {
            case Op::Jump: instr.a = target; break;
            case Op::JumpIfFalse:
            case Op::JumpIfTrue:
            case Op::JumpIfNull:
            case Op::JumpIfNotNull: instr.b = target; break;
            case Op::Compare: case Op::CompareInt: case Op::CompareIntK: instr.c = target; break;
            default: throw std::logic_error("patching an instruction that does not jump");
        }
    }

    int temp() {
        int reg = next++;
        p.registers = std::max(p.registers, next);
        return reg;
    }

    int text(const std::string& value) {
        auto found = textIndex.find(value);
        if (found != textIndex.end()) return found->second;
        if (p.texts.size() >= 0x7fff) throw std::runtime_error("Compile Error: too many operators in one function");
        p.texts.push_back(value);
        return textIndex[value] = static_cast<int>(p.texts.size()) - 1;
    }

    int addConstant(Value value) {
        p.constants.push_back(std::move(value));
        return static_cast<int>(p.constants.size()) - 1;
    }

    int constant(const Value& value) {
        switch (value.kind()) {
            case Value::Kind::Int: {
                auto found = ints.find(value.asInt());
                if (found != ints.end()) return found->second;
                return ints[value.asInt()] = addConstant(value);
            }
            case Value::Kind::Float: {
                double real = value.asFloat();
                std::uint64_t bits;
                std::memcpy(&bits, &real, sizeof bits);
                auto found = reals.find(bits);
                if (found != reals.end()) return found->second;
                return reals[bits] = addConstant(value);
            }
            case Value::Kind::Bool: {
                int& slot = bools[value.asBool() ? 1 : 0];
                if (slot < 0) slot = addConstant(value);
                return slot;
            }
            default:
                return addConstant(value);
        }
    }

    int stringConstant(const std::string& value) {
        auto found = strings.find(value);
        if (found != strings.end()) return found->second;
        return strings[value] = addConstant(Value::string(value));
    }

    // A register that holds the constant for the whole call: operands of arithmetic
    // read it like a variable.
    int constantRegister(int index) {
        auto found = constantRegisters.find(index);
        if (found != constantRegisters.end()) return found->second;
        constantOrder.push_back(index);
        return constantRegisters[index] = constantBase + static_cast<int>(constantOrder.size()) - 1;
    }

    // A name that is not local: a global, or in code compiled for a scope, looked up
    // there. `root` asks for the global itself, as `global int x` does.
    int globalSite(const std::string& name, bool root = false) {
        bool byName = names && !root;
        std::string key = (byName ? "~" : "") + name;
        auto found = globalSites.find(key);
        if (found != globalSites.end()) return found->second;
        p.globals.push_back({});
        p.globals.back().name = name;
        p.globals.back().byName = byName;
        return globalSites[key] = static_cast<int>(p.globals.size()) - 1;
    }

    int callSite(const std::string& name) {
        auto found = callSites.find(name);
        if (found != callSites.end()) return found->second;
        p.calls.push_back({});
        p.calls.back().name = name;
        return callSites[name] = static_cast<int>(p.calls.size()) - 1;
    }

    int conversion(Value::Kind kind, const std::string& type, const std::string& what) {
        p.conversions.push_back({kind, type, what});
        return static_cast<int>(p.conversions.size()) - 1;
    }

    void fail(const std::string& message) { emit(Op::Fail, stringConstant(message)); }

    // An error that belongs to the whole function, such as a break outside a loop: raised
    // after the code, where no try block of the function can catch it.
    void failAfter(const std::string& message) { late.push_back({emit(Op::Jump), line, message}); }

public:
    // The errors raised after the code go at its end.
    void epilogue() {
        for (const auto& error : late) {
            patch(error.jump, here());
            line = error.line;
            fail(error.message);
        }
        late.clear();
    }

private:
    struct LateError {
        int jump;
        int line;
        std::string message;
    };
    std::vector<LateError> late;

    // A local variable read and written in its own register: not captured by a lambda.
    bool isSlot(const VarRef& ref) const { return ref.slot >= 0 && !boxed(ref.slot); }
    bool constantVariable(const VarRef& ref) const {
        if (ref.slot >= 0 && slotConstants && static_cast<size_t>(ref.slot) < slotConstants->size())
            return (*slotConstants)[static_cast<size_t>(ref.slot)];
        if (ref.slot == VarRef::captured && ref.capture >= 0 && static_cast<size_t>(ref.capture) < p.captures.size())
            return p.captures[static_cast<size_t>(ref.capture)].constant;
        return false;
    }

    // The declared type of a local or captured variable, "" when unknown.
    std::string declaredType(const VarRef& ref) const {
        if (ref.slot >= 0 && slotTypes && static_cast<size_t>(ref.slot) < slotTypes->size())
            return (*slotTypes)[static_cast<size_t>(ref.slot)];
        if (ref.slot == VarRef::captured && ref.capture >= 0 && static_cast<size_t>(ref.capture) < p.captures.size())
            return p.captures[static_cast<size_t>(ref.capture)].type;
        return "";
    }

    bool boxed(int slot) const {
        return boxedSlots && slot >= 0 && static_cast<size_t>(slot) < boxedSlots->size() && (*boxedSlots)[static_cast<size_t>(slot)];
    }

    // A variable's value into dest, wherever it lives.
    void readVariable(const VarRef& ref, const std::string& name, int dest) {
        if (ref.slot >= 0 && boxed(ref.slot)) {
            emit(Op::Unbox, dest, ref.slot);
        } else if (ref.slot >= 0) {
            if (ref.slot != dest) emit(Op::Move, dest, ref.slot);
        } else if (ref.slot == VarRef::captured) {
            emit(Op::GetCapture, dest, ref.capture);
        } else {
            emit(Op::GetGlobal, dest, globalSite(name));
        }
    }

    // A declaration's value, computed by `store` into the register it is given, lands in
    // the slot: directly, or in a new box when a lambda captures the variable. The box
    // exists before the value is computed, so a lambda can call itself by its variable.
    template <typename Store>
    void declareSlot(int slot, Store store) {
        if (!boxed(slot)) {
            store(slot);
        } else {
            emit(Op::Clear, slot, slot + 1);
            emit(Op::Box, slot);
            int reg = temp();
            store(reg);
            emit(Op::BoxStore, slot, reg);
        }
        // The debugger shows a declared variable even while it holds null.
        if (debug) emit(Op::Declared, slot);
    }

    // A variable at the program's own top level lives in a register, and code that
    // looks it up by name (functions, modules, field defaults) finds it there once
    // its declaration has run. Declaring a name that is already a global is an error,
    // as it was when these variables were globals.
    int programGlobal(const std::string& name, const std::string& type, bool constant) {
        int site = globalSite(name, true);
        GlobalSite& global = p.globals[static_cast<size_t>(site)];
        if (isNullable(type)) global.nullable = type;
        if (constant) global.declaresConstant = true;
        emit(Op::DefineGlobal, -1, site, 1);
        return site;
    }

    // ---------------------------------------------------------------- expressions

    // A register holding the expression's value: a variable's own register, a constant
    // register, or a new temporary.
    int any(Node& node) {
        if (auto* n = dynamic_cast<VarAccessNode*>(&node)) {
            if (isSlot(n->ref)) return n->ref.slot;
        } else if (auto* n = dynamic_cast<BoolNode*>(&node)) {
            return constantRegister(constant(Value::boolean(n->val)));
        } else if (auto* n = dynamic_cast<NumberNode*>(&node)) {
            Value value;
            if (literal(*n, value)) return constantRegister(constant(value));
        }
        int reg = temp();
        into(node, reg);
        return reg;
    }

    static bool literal(NumberNode& node, Value& out) { return node.value(out); }

    // An int literal that fits an instruction: `n - 1`, `i < 100` need no register for it.
    static bool intLiteral(Node& node, int& out) {
        auto* number = dynamic_cast<NumberNode*>(&node);
        Value value;
        if (!number || number->isFloat || !literal(*number, value) || !value.isInt()) return false;
        if (value.asInt() < -2147483647LL - 1 || value.asInt() > 2147483647LL) return false;
        out = static_cast<int>(value.asInt());
        return true;
    }

    // The left operand of an operator, copied when the right one could change it.
    int operand(Node& left, Node& right) {
        int reg = any(left);
        if (reg < p.slots && mutates(&right)) {
            int copy = temp();
            emit(Op::Move, copy, reg);
            return copy;
        }
        return reg;
    }

    // The type an expression is sure to have, or "" when only running it tells: a
    // literal, a variable of a plain declared type, arithmetic on those. A value of the
    // very type a variable declares needs no conversion on the way in.
    std::string staticType(Node& node) const {
        if (auto* n = dynamic_cast<NumberNode*>(&node)) {
            Value value;
            if (!literal(*n, value)) return "";
            if (n->isFloat) return "float";
            // A literal beyond int's range is kept for print() but converts with an error.
            return runtime::storesAsIs(Value::Kind::Int, value) ? "int" : "";
        }
        if (dynamic_cast<StringNode*>(&node) || dynamic_cast<InterpolationNode*>(&node)) return "string";
        if (dynamic_cast<BoolNode*>(&node)) return "bool";
        if (auto* n = dynamic_cast<VarAccessNode*>(&node)) {
            std::string type = declaredType(n->ref);
            return type == "int" || type == "float" || type == "string" || type == "bool" ? type : "";
        }
        if (auto* n = dynamic_cast<UnaryOpNode*>(&node)) {
            if (n->op == "!") return "bool";
            std::string type = staticType(*n->operand);
            return type == "int" || type == "float" ? type : "";
        }
        if (auto* n = dynamic_cast<BinOpNode*>(&node)) {
            using runtime::Operator;
            switch (n->kind) {
                case Operator::And: case Operator::Or: case Operator::Eq: case Operator::Ne:
                case Operator::Lt: case Operator::Le: case Operator::Gt: case Operator::Ge:
                    return "bool";
                case Operator::Add: case Operator::Sub: case Operator::Mul: case Operator::Div: case Operator::Mod: {
                    std::string left = staticType(*n->left), right = staticType(*n->right);
                    if (n->kind == Operator::Add && (left == "string" || right == "string")) return "string";
                    bool numbers = (left == "int" || left == "float") && (right == "int" || right == "float");
                    if (!numbers) return "";
                    return left == "float" || right == "float" ? "float" : "int";
                }
                default:
                    return "";
            }
        }
        return "";
    }

    // The conversion a value needs to go into a variable of `type`, unless it is sure
    // to have that type already.
    void coerceUnlessKnown(int reg, Node* value, Value::Kind kind, const std::string& type, const std::string& what) {
        if (value && staticType(*value) == type) return;
        emit(Op::Coerce, reg, conversion(kind, type, what));
    }

    // Compiles the expression so that its value ends up in `dest`.
    void into(Node& node, int dest) {
        int mark = std::max(next, dest + 1);
        if (dest >= next) next = dest + 1;
        expression(node, dest);
        next = mark;
    }

    void expression(Node& node, int dest) {
        if (auto* n = dynamic_cast<NumberNode*>(&node)) {
            Value value;
            if (literal(*n, value)) emit(Op::LoadConst, dest, constant(value));
            else fail("Runtime Error: int literal '" + n->val + "' does not fit in int (int holds -2147483648..2147483647)");
        } else if (auto* n = dynamic_cast<StringNode*>(&node)) {
            emit(Op::LoadConst, dest, stringConstant(n->val));
        } else if (auto* n = dynamic_cast<BoolNode*>(&node)) {
            emit(Op::LoadConst, dest, constant(Value::boolean(n->val)));
        } else if (dynamic_cast<NullNode*>(&node)) {
            emit(Op::LoadConst, dest, constant(Value()));
        } else if (auto* n = dynamic_cast<VarAccessNode*>(&node)) {
            readVariable(n->ref, n->name, dest);
        } else if (auto* n = dynamic_cast<LambdaNode*>(&node)) {
            p.lambdas.push_back(compileLambda(*n));
            auto callee = std::make_shared<Callee>();
            callee->name = "lambda";
            callee->lambda = p.lambdas.back();
            p.callees.push_back(callee);
            emit(Op::Closure, dest, static_cast<int>(p.lambdas.size()) - 1);
        } else if (auto* n = dynamic_cast<BinOpNode*>(&node)) {
            binary(*n, dest);
        } else if (auto* n = dynamic_cast<UnaryOpNode*>(&node)) {
            int reg = any(*n->operand);
            emit(n->op == "!" ? Op::Not : Op::Negate, dest, reg);
        } else if (auto* n = dynamic_cast<PostIncNode*>(&node)) {
            increment(*n, dest);
        } else if (auto* n = dynamic_cast<FuncCallNode*>(&node)) {
            call(*n, dest);
        } else if (auto* n = dynamic_cast<MethodCallNode*>(&node)) {
            if (n->optional) {
                // base?.name(args): null without calling when base is null.
                int base = any(*n->base);
                emit(Op::Move, dest, base);
                int skip = emit(Op::JumpIfNull, dest);
                int first = next;
                emit(Op::Move, temp(), base);
                for (auto& arg : n->args) into(*arg, temp());
                emit(Op::Method, first, stringConstant(n->name), static_cast<int>(n->args.size()));
                emit(Op::Move, dest, first, 0, 1);
                next = first;
                patch(skip, here());
            } else {
                callThrough(Op::Method, *n->base, n->args, stringConstant(n->name), dest);
            }
        } else if (auto* n = dynamic_cast<CallNode*>(&node)) {
            callThrough(Op::CallValue, *n->callee, n->args, stringConstant("value"), dest);
        } else if (auto* n = dynamic_cast<IndexNode*>(&node)) {
            int base = operand(*n->base, *n->index);
            int index = any(*n->index);
            emit(Op::Index, dest, base, index);
        } else if (auto* n = dynamic_cast<FieldNode*>(&node)) {
            int base = any(*n->base);
            p.fields.push_back({n->name});
            if (n->optional) {
                // base?.name: null when base is null.
                int holder = temp();
                emit(Op::Move, holder, base);
                emit(Op::Move, dest, base);
                int skip = emit(Op::JumpIfNull, dest);
                emit(Op::Field, dest, holder, static_cast<int>(p.fields.size()) - 1);
                patch(skip, here());
            } else {
                emit(Op::Field, dest, base, static_cast<int>(p.fields.size()) - 1);
            }
        } else if (auto* n = dynamic_cast<InterpolationNode*>(&node)) {
            int first = next;
            for (auto& part : n->parts) into(*part, temp());
            emit(Op::Concat, dest, first, static_cast<int>(n->parts.size()));
        } else if (auto* n = dynamic_cast<ArrayLiteralNode*>(&node)) {
            int first = next;
            for (auto& element : n->elements) into(*element, temp());
            emit(Op::NewArray, dest, first, static_cast<int>(n->elements.size()));
        } else if (auto* n = dynamic_cast<MapLiteralNode*>(&node)) {
            int first = next;
            for (auto& entry : n->entries) {
                int key = temp();
                into(*entry.first, key);
                emit(Op::MapKey, key);
                into(*entry.second, temp());
            }
            emit(Op::NewMap, dest, first, static_cast<int>(n->entries.size()));
        } else {
            throw std::logic_error("the bytecode compiler does not know this expression");
        }
    }

    void binary(BinOpNode& node, int dest) {
        if (node.op == "??") {
            // The right side runs only when the left is null.
            into(*node.left, dest);
            int skip = emit(Op::JumpIfNotNull, dest);
            into(*node.right, dest);
            patch(skip, here());
            return;
        }
        int opText = text(node.op);
        if (node.kind == runtime::Operator::And || node.kind == runtime::Operator::Or) {
            // Short-circuit: the right side runs only when the left does not decide.
            into(*node.left, dest);
            emit(Op::Truth, dest, 0, 0, 0, opText);
            int skip = emit(node.kind == runtime::Operator::And ? Op::JumpIfFalse : Op::JumpIfTrue, dest, 0, 0, 3);
            into(*node.right, dest);
            emit(Op::Truth, dest, 0, 0, 1, opText);
            patch(skip, here());
            return;
        }
        static const Op ops[] = {Op::Add, Op::Sub, Op::Mul, Op::Div, Op::Mod, Op::Eq, Op::Ne, Op::Lt, Op::Le, Op::Gt, Op::Ge};
        if (node.kind == runtime::Operator::Unknown) {
            fail("Runtime Error: unknown operator '" + node.op + "'");
            return;
        }
        static const Op intOps[] = {Op::AddInt, Op::SubInt, Op::MulInt, Op::DivInt, Op::ModInt};
        bool ints = static_cast<int>(node.kind) <= static_cast<int>(runtime::Operator::Mod) &&
                    staticType(*node.left) == "int" && staticType(*node.right) == "int";
        int immediate;
        if (ints && (node.kind == runtime::Operator::Add || node.kind == runtime::Operator::Sub) &&
            intLiteral(*node.right, immediate)) {
            int left = any(*node.left);
            emit(node.kind == runtime::Operator::Add ? Op::AddIntK : Op::SubIntK, dest, left, immediate, 0, opText);
            return;
        }
        int left = operand(*node.left, *node.right);
        int right = any(*node.right);
        emit(ints ? intOps[static_cast<int>(node.kind)] : ops[static_cast<int>(node.kind)], dest, left, right, 0, opText);
    }

    // Where a call's first register goes: straight into dest when dest is the newest
    // temporary, else the next free register.
    int callBase(int dest) {
        if (dest == next - 1 && dest >= p.slots && dest < constantBase) {
            next = dest;
            return dest;
        }
        return next;
    }

    // first(args) or first.name(args): the value of `first` goes first, the arguments after it.
    void callThrough(Op op, Node& first, std::vector<std::unique_ptr<Node>>& args, int name, int dest) {
        int base = callBase(dest);
        into(first, temp());
        for (auto& arg : args) into(*arg, temp());
        emit(op, base, name, static_cast<int>(args.size()));
        if (base != dest) emit(Op::Move, dest, base, 0, 1);
    }

    // Whether a call's arguments can be read where they are: at most four, none of them
    // able to change a variable (no calls, assignments or ++), so reading a variable
    // late gives what reading it early would.
    bool direct(FuncCallNode& node) const {
        if (node.args.empty() || node.args.size() > 4 || p.argRegisters.size() + node.args.size() > 65535) return false;
        for (auto& arg : node.args)
            if (!pure(*arg)) return false;
        return true;
    }
    static bool pure(Node& node) {
        if (dynamic_cast<NumberNode*>(&node) || dynamic_cast<StringNode*>(&node) || dynamic_cast<BoolNode*>(&node) ||
            dynamic_cast<NullNode*>(&node) || dynamic_cast<VarAccessNode*>(&node))
            return true;
        if (auto* n = dynamic_cast<BinOpNode*>(&node)) return pure(*n->left) && pure(*n->right);
        if (auto* n = dynamic_cast<UnaryOpNode*>(&node)) return pure(*n->operand);
        if (auto* n = dynamic_cast<IndexNode*>(&node)) return pure(*n->base) && pure(*n->index);
        if (auto* n = dynamic_cast<FieldNode*>(&node)) return pure(*n->base);
        return false;
    }

    void call(FuncCallNode& node, int dest) {
        // The arguments go to consecutive registers; the result replaces the first.
        int base = callBase(dest);
        if (node.ref.slot >= 0 || node.ref.slot == VarRef::captured) {
            // A local variable holds the function: it goes first, the arguments after it.
            readVariable(node.ref, node.name, temp());
            for (auto& arg : node.args) into(*arg, temp());
            emit(Op::CallValue, base, stringConstant(node.name), static_cast<int>(node.args.size()));
        } else if (direct(node)) {
            // The arguments stay where they are; the call has its row of registers too,
            // for a callee that needs them there.
            // The row comes first, where the result goes; arguments that need a
            // register of their own get one after it, so none of them is in the row.
            for (size_t i = 0; i < node.args.size(); ++i) temp();
            std::vector<std::int32_t> registers;
            for (auto& arg : node.args) registers.push_back(any(*arg));
            int offset = static_cast<int>(p.argRegisters.size());
            p.argRegisters.insert(p.argRegisters.end(), registers.begin(), registers.end());
            emit(Op::CallDirect, base, callSite(node.name), static_cast<int>(node.args.size()), 0, offset);
        } else {
            for (auto& arg : node.args) into(*arg, temp());
            if (node.args.empty()) temp();
            emit(Op::Call, base, callSite(node.name), static_cast<int>(node.args.size()));
        }
        if (base != dest) emit(Op::Move, dest, base, 0, 1);
    }

    // name++ / name--; dest < 0 when the old value is not needed.
    void increment(PostIncNode& node, int dest) {
        if (constantVariable(node.ref)) {
            fail("Runtime Error: '" + node.name + "' is a constant and cannot be changed");
            return;
        }
        int step = stringConstant(node.name) * 2 + (node.delta > 0 ? 1 : 0);
        if (node.ref.slot >= 0 && boxed(node.ref.slot))
            emit(Op::IncrementRef, dest, node.ref.slot, step, 0);
        else if (node.ref.slot == VarRef::captured)
            emit(Op::IncrementRef, dest, node.ref.capture, step, 1);
        else if (isSlot(node.ref))
            emit(Op::Increment, dest, node.ref.slot, stringConstant(node.name) * 2 + (node.delta > 0 ? 1 : 0));
        else
            emit(Op::IncrementGlobal, dest, globalSite(node.name), node.delta > 0 ? 1 : 0);
    }

    // Jumps to target when the condition is `when`; returns the jump, to patch later.
    int conditionJump(Node& condition, bool when, const char* statement) {
        auto* comparison = dynamic_cast<BinOpNode*>(&condition);
        if (comparison && isComparison(comparison->kind)) {
            // A comparison always gives a bool, so it jumps directly.
            bool ints = staticType(*comparison->left) == "int" && staticType(*comparison->right) == "int";
            int immediate;
            if (ints && intLiteral(*comparison->right, immediate))
                return emit(Op::CompareIntK, any(*comparison->left), immediate, 0, static_cast<int>(comparison->kind),
                            text(comparison->op) * 2 + (when ? 1 : 0));
            int left = operand(*comparison->left, *comparison->right);
            int right = any(*comparison->right);
            return emit(ints ? Op::CompareInt : Op::Compare, left, right, 0, static_cast<int>(comparison->kind),
                        text(comparison->op) * 2 + (when ? 1 : 0));
        }
        int reg = any(condition);
        int kind = std::strcmp(statement, "if") == 0 ? 0 : std::strcmp(statement, "while") == 0 ? 1 : 2;
        return emit(when ? Op::JumpIfTrue : Op::JumpIfFalse, reg, 0, 0, kind);
    }

    // ---------------------------------------------------------------- statements

    void variable(VarDeclNode& node) {
        if (node.duplicate) {
            fail("Runtime Error: Variable '" + node.name + "' is already declared in this scope");
            return;
        }
        if (isSlotted(node.slot)) {
            int site = node.programGlobal ? programGlobal(node.name, node.type, node.constant) : -1;
            declareSlot(node.slot, [&](int reg) {
                if (node.expr) into(*node.expr, reg);
                else emit(Op::Zero, reg, stringConstant(node.type));
                coerceUnlessKnown(reg, node.expr.get(), node.kind, node.type, "variable '" + node.name + "'");
            });
            if (site >= 0) emit(Op::Declared, node.slot, site + 1);
            return;
        }
        int site = globalSite(node.name, node.global);
        if (isNullable(node.type)) p.globals[static_cast<size_t>(site)].nullable = node.type;
        if (node.constant) p.globals[static_cast<size_t>(site)].declaresConstant = true;
        if (!node.global) emit(Op::DefineGlobal, -1, site, 1);
        int reg = temp();
        if (node.expr) into(*node.expr, reg);
        else emit(Op::Zero, reg, stringConstant(node.type));
        emit(Op::Coerce, reg,
             conversion(node.kind, node.type, std::string(node.global ? "global variable '" : "variable '") + node.name + "'"));
        emit(Op::DefineGlobal, reg, site, 0);
    }

    void arrayVariable(ArrayDeclNode& node) {
        if (node.duplicate) {
            fail("Runtime Error: Variable '" + node.name + "' is already declared in this scope");
            return;
        }
        bool slotted = isSlotted(node.slot);
        int site = slotted ? -1 : globalSite(node.name, node.global);
        if (!slotted && node.constant) p.globals[static_cast<size_t>(site)].declaresConstant = true;
        if (!slotted && isNullable(node.type)) p.globals[static_cast<size_t>(site)].nullable = node.type;
        if (!slotted && !node.global) emit(Op::DefineGlobal, -1, site, 1);
        bool plain = node.type == "array";
        auto store = [&](int reg) {
            if (node.initializer) {
                into(*node.initializer, reg);
                emit(Op::Coerce, reg, conversion(runtime::declaredKind(node.type), node.type, "initializer of array '" + node.name + "'"));
            } else if (plain) {
                emit(Op::NewArray, reg, 0, 0);
            } else {
                emit(Op::Zero, reg, stringConstant(node.type)); // array<int>: typed and empty; array?: null
            }
        };
        if (slotted) {
            int site = node.programGlobal ? programGlobal(node.name, node.type, node.constant) : -1;
            declareSlot(node.slot, store);
            if (site >= 0) emit(Op::Declared, node.slot, site + 1);
            return;
        }
        int reg = temp();
        store(reg);
        emit(Op::DefineGlobal, reg, site, 0);
    }

    bool isSlotted(int slot) const { return slot >= 0; }

    // Whether compiling the expression into a register writes it only with its last
    // instruction, so the expression may still read the variable being assigned.
    static bool writesLast(Node& node) {
        if (auto* n = dynamic_cast<BinOpNode*>(&node))
            return n->kind != runtime::Operator::And && n->kind != runtime::Operator::Or &&
                   n->kind != runtime::Operator::Unknown;
        if (auto* n = dynamic_cast<UnaryOpNode*>(&node)) return n->op == "-" || n->op == "!";
        return dynamic_cast<NumberNode*>(&node) || dynamic_cast<StringNode*>(&node) || dynamic_cast<BoolNode*>(&node) ||
               dynamic_cast<VarAccessNode*>(&node);
    }

    void assignment(VarAssignNode& node) {
        if (constantVariable(node.ref)) {
            fail("Runtime Error: '" + node.name + "' is a constant and cannot be changed");
            return;
        }
        std::string type = declaredType(node.ref);
        // total = total + x on an int variable: computed straight into its register when
        // the value is sure to be of its type and the expression writes its result last.
        if (isSlot(node.ref) && !type.empty() && !isNullable(type) && staticType(*node.expr) == type &&
            writesLast(*node.expr)) {
            into(*node.expr, node.ref.slot);
            return;
        }
        int reg = temp();
        into(*node.expr, reg);
        if (isNullable(type)) {
            // The value may be null or of the type: converted here, then stored as it is.
            emit(Op::Coerce, reg, conversion(runtime::declaredKind(type), type, "variable '" + node.name + "'"));
            if (isSlot(node.ref)) emit(Op::Move, node.ref.slot, reg, 0, 1);
            else if (node.ref.slot >= 0) emit(Op::BoxStore, node.ref.slot, reg);
            else emit(Op::SetCapture, node.ref.capture, reg, stringConstant(node.name), 1);
            return;
        }
        if (isSlot(node.ref)) emit(Op::Assign, node.ref.slot, reg, stringConstant(node.name));
        else if (node.ref.slot >= 0) emit(Op::BoxAssign, node.ref.slot, reg, stringConstant(node.name));
        else if (node.ref.slot == VarRef::captured) emit(Op::SetCapture, node.ref.capture, reg, stringConstant(node.name));
        else emit(Op::SetGlobal, reg, globalSite(node.name));
    }

    void setPath(SetNode& node) {
        // The value first: it may change the containers the target lives in. Then the
        // indexes, from the outermost in, as Skrumi always has.
        int value = temp();
        into(*node.value, value);
        SetPath path;
        path.op = node.op;
        path.kind = runtime::operatorOf(node.op);
        std::vector<SetPath::Step> steps;
        Node* at = node.target.get();
        for (;;) {
            if (auto* index = dynamic_cast<IndexNode*>(at)) {
                int key = temp();
                into(*index->index, key);
                steps.push_back({false, key, ""});
                at = index->base.get();
            } else if (auto* field = dynamic_cast<FieldNode*>(at)) {
                steps.push_back({true, -1, field->name});
                at = field->base.get();
            } else {
                break;
            }
        }
        auto* variable = dynamic_cast<VarAccessNode*>(at);
        if (!variable || steps.empty()) {
            fail("Syntax Error: only a variable, an element or a field can be assigned to");
            return;
        }
        std::reverse(steps.begin(), steps.end());
        path.steps = std::move(steps);
        path.variable = variable->name;
        if (variable->ref.slot >= 0) {
            path.slot = variable->ref.slot;
            path.boxed = boxed(variable->ref.slot);
        } else if (variable->ref.slot == VarRef::captured) {
            path.capture = variable->ref.capture;
        } else {
            path.global = globalSite(variable->name);
        }
        p.paths.push_back(std::move(path));
        emit(Op::SetPath, value, static_cast<int>(p.paths.size()) - 1);
    }

    void returnStatement(ReturnNode& node) {
        int reg = -1;
        if (node.expr) {
            reg = any(*node.expr);
            // finally blocks run before the function returns and may change the variable.
            if (!tries.empty() && reg < p.slots) {
                int copy = temp();
                emit(Op::Move, copy, reg);
                reg = copy;
            }
        }
        if (!function) {
            if (!leave(0)) return;
            failAfter(outside.empty() ? "Runtime Error: 'return' outside of a function" : outside);
            return;
        }
        if (!leave(0)) return;
        if (reg >= 0) emit(Op::Return, reg);
        else emit(Op::ReturnVoid);
    }

    // break and continue leave blocks, loops and try statements; the finally blocks on
    // the way run first.
    void jumpOut(bool isContinue) {
        Target* target = nullptr;
        for (auto it = targets.rbegin(); it != targets.rend(); ++it) {
            if (!isContinue || it->loop) {
                target = &*it;
                break;
            }
        }
        if (!target) {
            if (!leave(0)) return;
            std::string word = isContinue ? "continue" : "break";
            if (!outside.empty()) failAfter(outside);
            else failAfter(function ? "Runtime Error: '" + word + "' outside of loop in function '" + p.name + "'"
                               : "Runtime Error: '" + word + "' outside of loop in global scope");
            return;
        }
        size_t index = static_cast<size_t>(target - targets.data());
        if (!leave(target->tries)) return;
        target = &targets[index];
        for (int open = scopes; open > target->scopes; --open) emit(Op::ScopeLeave);
        if (isContinue) {
            if (target->clearTo > target->clearFrom) emit(Op::Clear, target->clearFrom, target->clearTo);
            target->continues.push_back(emit(Op::Jump));
        } else {
            target->breaks.push_back(emit(Op::Jump));
        }
    }

    // Leaves the try statements above `depth`: runs their finally blocks. False when
    // the code is inside a finally that runs for an error, which raises it again instead.
    bool leave(size_t depth) {
        for (size_t i = tries.size(); i-- > depth;) {
            TryContext context = tries[i];
            if (context.part == Part::Rescue) {
                emit(Op::Rethrow, context.pending);
                return false;
            }
            if (context.part == Part::Body) emit(Op::TryLeave);
            if (context.node->cleanup && context.part != Part::Finally) inlineFinally(*context.node, i);
        }
        return true;
    }

    // A copy of a finally block, run outside its try statement.
    void inlineFinally(TryNode& node, size_t depth) {
        auto saved = tries;
        int savedScopes = scopes;
        tries.resize(depth);
        tries.push_back({&node, Part::Finally, -1});
        auto* cleanup = dynamic_cast<BlockNode*>(node.cleanup.get());
        if (cleanup) block(*cleanup);
        else statement(*node.cleanup);
        tries = std::move(saved);
        scopes = savedScopes;
    }

    void ifStatement(IfNode& node) {
        int skip = conditionJump(*node.condition, false, "if");
        if (node.thenB) statement(*node.thenB);
        if (node.elseB) {
            int end = emit(Op::Jump);
            patch(skip, here());
            statement(*node.elseB);
            patch(end, here());
        } else {
            patch(skip, here());
        }
    }

    Target loopTarget(Node* body) {
        Target target{true, {}, {}, tries.size(), scopes};
        if (auto* block = dynamic_cast<BlockNode*>(body)) {
            target.clearFrom = block->firstSlot;
            target.clearTo = block->endSlot;
        }
        return target;
    }

    void loopBody(Node* body) {
        if (!body) return;
        if (auto* block = dynamic_cast<BlockNode*>(body)) this->block(*block, true);
        else statement(*body);
    }

    void whileLoop(WhileNode& node) {
        int toCondition = emit(Op::Jump);
        int start = here();
        targets.push_back(loopTarget(node.body.get()));
        loopBody(node.body.get());
        Target target = std::move(targets.back());
        targets.pop_back();
        for (int jump : target.continues) patch(jump, here());
        patch(toCondition, here());
        patch(conditionJump(*node.condition, true, "while"), start);
        for (int jump : target.breaks) patch(jump, here());
        if (target.clearTo > target.clearFrom) emit(Op::Clear, target.clearFrom, target.clearTo);
    }

    void forLoop(ForNode& node) {
        if (node.init) statement(*node.init);
        int toCondition = node.condition ? emit(Op::Jump) : -1;
        int start = here();
        targets.push_back(loopTarget(node.body.get()));
        loopBody(node.body.get());
        Target target = std::move(targets.back());
        targets.pop_back();
        for (int jump : target.continues) patch(jump, here());
        if (node.step) statement(*node.step);
        if (node.condition) {
            patch(toCondition, here());
            patch(conditionJump(*node.condition, true, "for"), start);
        } else {
            emit(Op::Jump, start);
        }
        for (int jump : target.breaks) patch(jump, here());
        if (node.endSlot > node.firstSlot) emit(Op::Clear, node.firstSlot, node.endSlot);
    }

    void forIn(ForInNode& node) {
        int container = temp();
        into(*node.iterable, container);
        int zero = constant(Value::integer(0));
        emit(Op::LoadConst, temp(), zero);
        emit(Op::LoadConst, temp(), zero);
        int start = emit(Op::ForIn, container, node.variables.front().slot, 0, static_cast<int>(node.variables.size()));
        for (const auto& variable : node.variables)
            if (!variable.type.empty())
                emit(Op::Coerce, variable.slot,
                     conversion(runtime::declaredKind(variable.type), variable.type, "variable '" + variable.name + "'"));
        for (const auto& variable : node.variables)
            if (boxed(variable.slot)) emit(Op::Box, variable.slot); // a new box every round
        targets.push_back(loopTarget(node.body.get()));
        loopBody(node.body.get());
        Target target = std::move(targets.back());
        targets.pop_back();
        for (int jump : target.continues) patch(jump, start);
        emit(Op::Jump, start);
        p.code[static_cast<size_t>(start)].c = here();
        for (int jump : target.breaks) patch(jump, here());
        if (node.endSlot > node.firstSlot) emit(Op::Clear, node.firstSlot, node.endSlot);
    }

    void switchStatement(SwitchNode& node) {
        int value = temp();
        into(*node.expr, value);
        // The cases are tried in order until one matches; from its body on, execution
        // falls through the following bodies and the default, until break.
        std::vector<int> toBody;
        for (auto& item : node.cases) {
            int mark = next;
            int candidate = any(*item.first);
            int matched = temp();
            emit(Op::Match, matched, value, candidate);
            toBody.push_back(emit(Op::JumpIfTrue, matched, 0, 0, 3));
            next = mark;
        }
        int toDefault = emit(Op::Jump);
        targets.push_back({false, {}, {}, tries.size(), scopes});
        for (size_t i = 0; i < node.cases.size(); ++i) {
            patch(toBody[i], here());
            statement(*node.cases[i].second);
        }
        patch(toDefault, here());
        if (node.defaultCase) statement(*node.defaultCase);
        Target target = std::move(targets.back());
        targets.pop_back();
        for (int jump : target.breaks) patch(jump, here());
    }

    void tryStatement(TryNode& node) {
        auto* body = dynamic_cast<BlockNode*>(node.body.get());
        auto* handler = dynamic_cast<BlockNode*>(node.handler.get());
        int level = ++tryLevel * 2;
        int pending = node.cleanup ? p.pendingErrors++ : -1;
        int outerScopes = scopes;
        int bodyFrom = body ? body->firstSlot : 0, bodyTo = body ? body->endSlot : 0;
        int handlerTo = handler ? handler->endSlot : bodyTo;

        int bodyStart = emit(Op::TryEnter);
        tries.push_back({&node, Part::Body, pending});
        statement(*node.body);
        tries.pop_back();
        int bodyEnd = emit(Op::TryLeave);
        p.tryBodies.push_back({bodyStart + 1, bodyEnd});
        if (node.cleanup) inlineFinally(node, tries.size());
        std::vector<int> toEnd{emit(Op::Jump)};

        int handlerStart = here(), handlerEnd = here();
        if (node.handler) {
            Handler caught{bodyStart, bodyEnd, handlerStart, level + 1, false};
            caught.message = node.errorName.empty() ? -1 : node.errorSlot;
            caught.clearFrom = bodyFrom;
            caught.clearTo = bodyTo;
            caught.scopes = outerScopes;
            p.handlers.push_back(caught);
            tries.push_back({&node, Part::Handler, pending});
            if (boxed(node.errorSlot)) emit(Op::Box, node.errorSlot);
            statement(*node.handler);
            tries.pop_back();
            if (node.errorSlot >= 0 && !node.errorName.empty()) emit(Op::Clear, node.errorSlot, node.errorSlot + 1);
            handlerEnd = here();
            if (node.cleanup) inlineFinally(node, tries.size());
            toEnd.push_back(emit(Op::Jump));
        }
        if (node.cleanup) {
            int rescue = here();
            Handler cleanup{bodyStart, bodyEnd, rescue, level, true};
            cleanup.pending = pending;
            cleanup.clearFrom = bodyFrom;
            cleanup.clearTo = handlerTo;
            cleanup.scopes = outerScopes;
            p.handlers.push_back(cleanup);
            if (handlerEnd > handlerStart) {
                cleanup.start = handlerStart;
                cleanup.end = handlerEnd;
                p.handlers.push_back(cleanup);
            }
            tries.push_back({&node, Part::Rescue, pending});
            statement(*node.cleanup);
            tries.pop_back();
            emit(Op::Rethrow, pending);
        }
        for (int jump : toEnd) patch(jump, here());
        --tryLevel;
    }
};

} // namespace

// The call site a body forwards its parameters to, or -1 (see Proto::forward).
int forwardedCall(const FuncDefNode& function, const Proto& proto) {
    const BlockNode& body = *function.block();
    if (body.stmts.size() != 1 || proto.method || proto.debug) return -1;
    const Node* statement = body.stmts[0].get();
    if (auto* returned = dynamic_cast<const ReturnNode*>(statement)) statement = returned->expr.get();
    else if (proto.result.kind != Value::Kind::Void) return -1;
    auto* call = dynamic_cast<const FuncCallNode*>(statement);
    if (!call || call->ref.slot >= 0 || call->ref.slot == VarRef::captured) return -1;
    if (call->args.size() != function.params.size()) return -1;
    for (size_t i = 0; i < call->args.size(); ++i) {
        auto* arg = dynamic_cast<const VarAccessNode*>(call->args[i].get());
        if (!arg || arg->ref.slot != static_cast<int>(i) || body.layout->boxed[i]) return -1;
    }
    for (size_t i = 0; i < proto.calls.size(); ++i)
        if (proto.calls[i].name == call->name) return static_cast<int>(i);
    return -1;
}

std::shared_ptr<Proto> compileFunction(const FuncDefNode& function, bool debug) {
    BlockNode* body = function.block();
    if (!body) throw std::logic_error("a function body must be a block");
    if (!body->layout) resolveFunction(const_cast<FuncDefNode&>(function));
    auto proto = std::make_shared<Proto>();
    proto->name = function.name;
    proto->file = body->file;
    proto->line = function.range.start.line;
    proto->debug = debug;
    proto->slotNames = std::make_shared<std::vector<std::string>>(body->layout->names);
    proto->slots = static_cast<int>(body->layout->names.size());
    proto->method = !function.params.empty() && function.params[0].name == "this" &&
                    function.name.find('.') != std::string::npos;
    for (const auto& param : function.params) {
        if (param.type.empty()) proto->params.push_back({Value::Kind::Void, "", ""});
        else proto->params.push_back({runtime::declaredKind(param.type), param.type,
                                      "parameter '" + param.name + "' of '" + function.name + "'"});
    }
    proto->result = {runtime::declaredKind(function.returnType), function.returnType, "return value of '" + function.name + "'"};
    Compiler compiler(*proto, debug, true);
    compiler.boxedSlots = &body->layout->boxed;
    compiler.slotTypes = &body->layout->types;
    compiler.slotConstants = &body->layout->constants;
    compiler.boxParameters(function.params.size());
    compiler.block(*body, false, true);
    compiler.end(body->range.end.line);
    compiler.epilogue();
    compiler.finish();
    proto->forward = forwardedCall(function, *proto);
    return proto;
}

std::shared_ptr<Proto> compileStatements(BlockNode& statements, const std::string& outside) {
    resolveProgram(statements);
    auto proto = std::make_shared<Proto>();
    proto->file = statements.file;
    proto->slotNames = std::make_shared<std::vector<std::string>>(statements.layout->names);
    proto->slots = static_cast<int>(statements.layout->names.size());
    Compiler compiler(*proto, false, false);
    compiler.names = true;
    compiler.outside = outside;
    compiler.boxedSlots = &statements.layout->boxed;
    compiler.slotTypes = &statements.layout->types;
    compiler.slotConstants = &statements.layout->constants;
    compiler.topLevel(statements, Unit::Module);
    compiler.end(statements.range.end.line);
    compiler.epilogue();
    compiler.finish();
    return proto;
}

std::shared_ptr<Proto> compileExpression(Node& expression) {
    resolveExpression(expression);
    auto proto = std::make_shared<Proto>();
    proto->slotNames = std::make_shared<std::vector<std::string>>();
    Compiler compiler(*proto, false, false);
    compiler.names = true;
    compiler.value(expression);
    compiler.epilogue();
    compiler.finish();
    return proto;
}

std::shared_ptr<Proto> compileProgram(BlockNode& program, Unit unit, bool debug) {
    resolveProgram(program, unit == Unit::Program);
    auto proto = std::make_shared<Proto>();
    proto->file = program.file;
    proto->debug = debug && unit == Unit::Program;
    proto->slotNames = std::make_shared<std::vector<std::string>>(program.layout->names);
    proto->slots = static_cast<int>(program.layout->names.size());
    Compiler compiler(*proto, proto->debug, false);
    compiler.boxedSlots = &program.layout->boxed;
    compiler.slotTypes = &program.layout->types;
    compiler.slotConstants = &program.layout->constants;
    if (unit == Unit::Program) {
        compiler.hoist(program);
        // As a function body: the top level's registers stay filled when it ends, so the
        // program's variables become globals (vm::run) for what runs after it.
        compiler.block(program, false, true);
    } else {
        compiler.topLevel(program, unit);
    }
    compiler.end(program.range.end.line);
    compiler.epilogue();
    compiler.finish();
    return proto;
}

namespace {

const char* opName(Op op) {
    static const char* names[] = {
        "move", "loadk", "clear", "getglobal", "setglobal", "defglobal", "coerce", "assign", "zero", "newsized",
        "fail", "add", "sub", "mul", "div", "mod", "eq", "ne", "lt", "le", "gt", "ge", "neg", "not", "truth",
        "jump", "jumpif-false", "jumpif-true", "jumpif-null", "jumpif-notnull", "compare", "for-in", "call", "return", "return-void", "newarray", "newmap",
        "mapkey", "concat", "index", "field", "setpath", "inc", "inc-global", "box", "unbox", "box-store", "box-assign", "get-capture",
        "set-capture", "inc-ref", "closure", "call-value", "method", "declare", "throw", "rethrow", "try-enter",
        "try-leave", "match", "statement", "scope-enter", "scope-leave", "declared",
        "add-int", "sub-int", "mul-int", "div-int", "mod-int", "compare-int", "call-direct", "add-int-k", "sub-int-k", "compare-int-k"};
    return names[static_cast<int>(op)];
}

std::string show(const Value& value) {
    if (value.isString()) {
        std::string text = value.str();
        if (text.size() > 40) text = text.substr(0, 37) + "...";
        std::string out = "\"";
        for (char ch : text) out += ch == '\n' ? std::string("\\n") : std::string(1, ch);
        return out + "\"";
    }
    return value.text();
}

} // namespace

void disassemble(const Proto& proto, std::ostream& out) {
    out << (proto.name.empty() ? std::string("<program>") : proto.name + "()") << ": " << proto.code.size()
        << " instructions, " << proto.registers << " registers (" << proto.slots << " variables)\n";
    for (size_t i = 0; i < static_cast<size_t>(proto.slots); ++i)
        out << "    r" << i << " = " << (*proto.slotNames)[i] << "\n";
    for (const auto& [reg, index] : proto.preload)
        out << "    r" << reg << " = " << show(proto.constants[static_cast<size_t>(index)]) << "\n";
    int lastLine = -1;
    for (size_t pc = 0; pc < proto.code.size(); ++pc) {
        const Instr& in = proto.code[pc];
        int line = proto.lines[pc];
        out << (line != lastLine ? std::to_string(line) : std::string()) ;
        out << std::string(line != lastLine ? std::max<size_t>(1, 6 - std::to_string(line).size()) : 6, ' ');
        lastLine = line;
        std::string index = std::to_string(pc);
        out << std::string(4 - std::min<size_t>(4, index.size()), ' ') << index << "  " << opName(in.op);
        std::string name = opName(in.op);
        out << std::string(name.size() < 13 ? 13 - name.size() : 1, ' ');
        auto reg = [](int r) { return "r" + std::to_string(r); };
        const auto& K = proto.constants;
        switch (in.op) {
            case Op::Move: out << reg(in.a) << " " << reg(in.b) << (in.x ? " (moved)" : ""); break;
            case Op::LoadConst: out << reg(in.a) << " " << show(K[in.b]); break;
            case Op::Clear: out << reg(in.a) << ".." << reg(in.b - 1); break;
            case Op::GetGlobal: out << reg(in.a) << " " << proto.globals[in.b].name; break;
            case Op::SetGlobal: out << proto.globals[in.b].name << " " << reg(in.a); break;
            case Op::DefineGlobal:
                out << proto.globals[in.b].name;
                if (in.a < 0) out << " (check)";
                else out << " " << reg(in.a);
                break;
            case Op::Coerce: out << reg(in.a) << " " << proto.conversions[in.b].type; break;
            case Op::Assign: out << reg(in.a) << " " << reg(in.b) << " (" << K[in.c].str() << ")"; break;
            case Op::Zero: out << reg(in.a) << " " << K[in.b].str(); break;
            case Op::Fail: out << show(K[in.a]); break;
            case Op::Add: case Op::Sub: case Op::Mul: case Op::Div: case Op::Mod:
            case Op::AddInt: case Op::SubInt: case Op::MulInt: case Op::DivInt: case Op::ModInt:
            case Op::Eq: case Op::Ne: case Op::Lt: case Op::Le: case Op::Gt: case Op::Ge:
                out << reg(in.a) << " " << reg(in.b) << " " << reg(in.c);
                break;
            case Op::AddIntK: case Op::SubIntK: out << reg(in.a) << " " << reg(in.b) << " " << in.c; break;
            case Op::Negate: case Op::Not: out << reg(in.a) << " " << reg(in.b); break;
            case Op::Truth: out << reg(in.a); break;
            case Op::Jump: out << "-> " << in.a; break;
            case Op::JumpIfFalse: case Op::JumpIfTrue: case Op::JumpIfNull: case Op::JumpIfNotNull:
                out << reg(in.a) << " -> " << in.b;
                break;
            case Op::Compare: case Op::CompareInt:
                out << "if " << (in.y & 1 ? "" : "not ") << reg(in.a) << " " << proto.texts[in.y >> 1] << " " << reg(in.b)
                    << " -> " << in.c;
                break;
            case Op::CompareIntK:
                out << "if " << (in.y & 1 ? "" : "not ") << reg(in.a) << " " << proto.texts[in.y >> 1] << " " << in.b
                    << " -> " << in.c;
                break;
            case Op::ForIn:
                out << reg(in.b) << (in.x == 2 ? ", " + reg(in.b + 1) : std::string()) << " in " << reg(in.a) << " else -> " << in.c;
                break;
            case Op::Call:
                out << reg(in.a) << " = " << proto.calls[in.b].name << "(";
                for (int i = 0; i < in.c; ++i) out << (i ? ", " : "") << reg(in.a + i);
                out << ")";
                break;
            case Op::CallDirect:
                out << reg(in.a) << " = " << proto.calls[in.b].name << "(";
                for (int i = 0; i < in.c; ++i) out << (i ? ", " : "") << reg(proto.argRegisters[static_cast<size_t>(in.y + i)]);
                out << ")";
                break;
            case Op::Return: out << reg(in.a); break;
            case Op::NewArray: out << reg(in.a) << " [" << in.c << " from " << reg(in.b) << "]"; break;
            case Op::NewMap: out << reg(in.a) << " {" << in.c << " from " << reg(in.b) << "}"; break;
            case Op::MapKey: out << reg(in.a); break;
            case Op::Concat: out << reg(in.a) << " [" << in.c << " from " << reg(in.b) << "]"; break;
            case Op::Index: out << reg(in.a) << " " << reg(in.b) << "[" << reg(in.c) << "]"; break;
            case Op::Field: out << reg(in.a) << " " << reg(in.b) << "." << proto.fields[in.c].name; break;
            case Op::SetPath: {
                const SetPath& path = proto.paths[in.b];
                out << path.variable;
                for (const auto& step : path.steps) out << (step.field ? "." + step.name : "[" + reg(step.key) + "]");
                out << " " << (path.op == "=" ? "=" : path.op + "=") << " " << reg(in.a);
                break;
            }
            case Op::Increment:
                if (in.a >= 0) out << reg(in.a) << " = ";
                out << reg(in.b) << (in.c & 1 ? "++" : "--");
                break;
            case Op::IncrementGlobal:
                if (in.a >= 0) out << reg(in.a) << " = ";
                out << proto.globals[in.b].name << (in.c ? "++" : "--");
                break;
            case Op::Declare: out << "#" << in.b << (in.c ? " (if absent)" : ""); break;
            case Op::Box: out << reg(in.a); break;
            case Op::Unbox: out << reg(in.a) << " <- box " << reg(in.b); break;
            case Op::BoxStore: case Op::BoxAssign: out << "box " << reg(in.a) << " <- " << reg(in.b); break;
            case Op::GetCapture: out << reg(in.a) << " <- capture " << in.b; break;
            case Op::SetCapture: out << "capture " << in.a << " <- " << reg(in.b); break;
            case Op::IncrementRef:
                if (in.a >= 0) out << reg(in.a) << " = ";
                out << (in.x ? "capture " + std::to_string(in.b) : "box " + reg(in.b)) << (in.c & 1 ? "++" : "--");
                break;
            case Op::Closure: out << reg(in.a) << " = lambda #" << in.b; break;
            case Op::Method:
                out << reg(in.a) << " = " << reg(in.a) << "." << proto.constants[static_cast<size_t>(in.b)].str() << "(";
                for (int i = 1; i <= in.c; ++i) out << (i > 1 ? ", " : "") << reg(in.a + i);
                out << ")";
                break;
            case Op::CallValue:
                out << reg(in.a) << " = " << reg(in.a) << "(";
                for (int i = 1; i <= in.c; ++i) out << (i > 1 ? ", " : "") << reg(in.a + i);
                out << ")";
                break;
            case Op::Throw: out << reg(in.a); break;
            case Op::Rethrow: out << "#" << in.a; break;
            case Op::Match: out << reg(in.a) << " " << reg(in.b) << " " << reg(in.c); break;
            case Op::Statement: out << "line " << in.a; break;
            default: break;
        }
        out << "\n";
    }
    for (const auto& handler : proto.handlers)
        out << "    " << (handler.finally ? "finally" : "catch") << " [" << handler.start << ", " << handler.end << ") -> "
            << handler.target << "\n";
}

} // namespace skrumi::bytecode
