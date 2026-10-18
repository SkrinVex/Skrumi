#pragma once
// Skrumi runs as bytecode: the parser's tree is compiled, one function at a time, into
// instructions for a register machine (Compiler.cpp) that the VM executes (Vm.cpp).
// A function's registers are its numbered variables (the resolver's slots, parameters
// first), then the constants its arithmetic uses, then temporaries.
#include "skrumi/AST.h"
#include "skrumi/Context.h"
#include "skrumi/Runtime.h"
#include <cstdint>
#include <memory>
#include <ostream>
#include <string>
#include <vector>

namespace skrumi {

struct Node;
struct Declaration;
struct BlockNode;
struct FuncDefNode;

namespace runtime { struct Builtin; }

namespace bytecode {

enum class Op : std::uint8_t {
    // Registers and constants
    Move,          // R[a] = R[b]
    LoadConst,     // R[a] = K[b]
    Clear,         // R[a] .. R[b-1] become empty: the variables of a block that ended
    // Globals, by a cached lookup per instruction
    GetGlobal,     // R[a] = global b
    SetGlobal,     // global b = R[a], converted to the variable's type
    DefineGlobal,  // declare global b = R[a]; c: 1 when a second declaration is an error
    // Declarations
    Coerce,        // R[a] converted for a slot of type b (a Conversion)
    Assign,        // R[a] = R[b] for the variable named c, converted to its type
    Zero,          // R[a] = the zero value of type K[b]
    Fail,          // raise the error text K[a]
    // Arithmetic and comparison: R[a] = R[b] op R[c]; y names the operator's text
    Add, Sub, Mul, Div, Mod, Eq, Ne, Lt, Le, Gt, Ge,
    Negate,        // R[a] = -R[b]
    Not,           // R[a] = !R[b]
    Truth,         // R[a] must be bool: operand x ("left"/"right") of the operator named y
    // Jumps; a condition must be bool, x names the statement for the error
    Jump,          // go to a
    JumpIfFalse,   // go to b unless R[a]
    JumpIfTrue,    // go to b if R[a]
    JumpIfNull,    // go to b if R[a] is null
    JumpIfNotNull, // go to b unless R[a] is null
    Compare,       // go to c when (R[a] op R[b]) == (y != 0); op is x
    ForIn,         // next round of for-in over R[a] (position R[a+1], count R[a+2]):
                   // fills the x loop variables from R[b]; at the end, go to c
    // Calls
    Call,          // R[a] = call site b with c arguments in R[a] .. R[a+c-1]
    Return,        // return R[a]
    ReturnVoid,
    // Containers
    NewArray,      // R[a] = [R[b] .. R[b+c-1]]
    NewMap,        // R[a] = {R[b]: R[b+1], ...} for c pairs
    MapKey,        // R[a] = the map key R[a] stands for: a string, or an int as text
    Concat,        // R[a] = the text of R[b] .. R[b+c-1] joined, as print() shows each
    Index,         // R[a] = R[b][R[c]]
    Field,         // R[a] = R[b].name, through field site c
    SetPath,       // store R[a] through path b: items[i].name = value
    Increment,     // R[a] = R[b]++ (or --): step c; a < 0 when the old value is not used
    IncrementGlobal, // the same on global b, into R[a]; step c
    // Variables that lambdas capture live in boxes
    Box,           // R[a] = a new box holding R[a]
    Unbox,         // R[a] = what box R[b] holds
    BoxStore,      // box R[a] holds R[b] (moved), as a declaration stores it
    BoxAssign,     // box R[a] holds R[b], converted to the variable's type (name K[c])
    GetCapture,    // R[a] = what captured variable b holds
    SetCapture,    // captured variable a = R[b], converted to its type (name K[c]); x=1: already converted
    IncrementRef,  // R[a] = old value of the box R[b] (x=0) or capture b (x=1); ++ when c&1, name K[c>>1]
    Closure,       // R[a] = lambda b of this code, with its captured variables
    CallValue,     // R[a] = call the function R[a] with c arguments in R[a+1] ..
    Method,        // R[a] = call method K[b] of R[a] with c arguments in R[a+1] ..; a field
                   // or map key of that name that holds a function is called without R[a]
    // Statements
    Declare,       // run declaration node b: a function, a struct, using or include;
                   // c: 1 to declare only a name that does not exist yet
    Throw,         // raise display(R[a])
    Rethrow,       // raise again the error that finally a is running for
    TryEnter,      // the program is inside a try block (the debugger asks)
    TryLeave,
    Match,         // R[a] = whether switch value R[b] matches case R[c]
    // Debugger (only in code compiled while one is attached)
    Statement,     // a statement at line a begins
    ScopeEnter,    // a block begins
    ScopeLeave,
    Declared,      // slot a now holds a declared variable, even when that is null
    // Arithmetic and jumps on operands the compiler knows are ints (typed variables,
    // int literals): only the result's range is checked. As Add .. Mod and Compare.
    AddInt, SubInt, MulInt, DivInt, ModInt,
    CompareInt,
    // R[a] = call site b with c arguments that stay where they are, in the registers
    // listed from argRegisters[y]; R[a] .. R[a+c-1] are there to copy them to when the
    // callee needs them in a row (every callee but a builtin's fast path).
    CallDirect,
    // AddInt and SubInt with an int literal on the right: R[a] = R[b] + c, R[b] - c,
    // the int itself in c. CompareIntK is CompareInt with the int itself in b.
    AddIntK, SubIntK, CompareIntK,
};

struct Instr {
    Op op;
    std::uint8_t x = 0;
    std::uint16_t y = 0;
    std::int32_t a = 0, b = 0, c = 0;
};

// Converting a value for a declared type: a parameter, a variable, a return value.
struct Conversion {
    Value::Kind kind;
    std::string type;
    std::string what; // "variable 'x'", for the error message
};

// A global variable read or written by name; the lookup is kept until the globals change.
struct GlobalSite {
    std::string name;
    std::string nullable; // a declaration of type T?: later assignments may store null
    bool declaresConstant = false; // this site defines a const global
    bool constant = false;         // the global it found is a constant: writes fail
    // Code compiled for a scope (the debugger's console, a field's default value) finds
    // the name through that scope: its blocks, the paused function's slots, the globals.
    bool byName = false;
    Value* cached = nullptr;
    const Context* root = nullptr;
    unsigned generation = 0;
    Value function; // the name of a function read as a value: func f = add;
};

// A call by name: a builtin, a Skrumi function or a struct's constructor.
struct CallSite {
    std::string name;
    bool resolved = false;
    const runtime::Builtin* builtin = nullptr;
    const FuncDefNode* function = nullptr; // valid while root->functionGeneration is
    const Context* root = nullptr;
    unsigned generation = 0;
};

// base.name: the field's position is remembered for the struct type last seen here.
struct FieldSite {
    std::string name;
    const StructType* type = nullptr;
    size_t index = 0;
};

// target = value through indexes and fields that start at a variable.
struct SetPath {
    struct Step {
        bool field;
        int key = -1;     // register holding the index, for [key]
        std::string name; // for .name
    };
    int slot = -1;        // the variable's register (holding a box when `boxed`), or
    bool boxed = false;
    int capture = -1;     // the lambda's captured variable, or
    int global = -1;      // its global site
    std::string variable;
    std::vector<Step> steps; // from the variable outwards
    std::string op;          // "=", or the operator of a compound assignment: "+"
    runtime::Operator kind = runtime::Operator::Unknown;
};

// A protected range of code: a catch block or a finally block handles an error raised
// at an instruction inside it. The innermost range (highest level) wins.
struct Handler {
    int start, end;   // instructions [start, end)
    int target;       // where the handler's code begins
    int level;
    bool finally;     // finally runs for any error, catch only for runtime errors
    int message = -1; // catch: the register that receives the error text
    int pending = -1; // finally: where the error waits while finally runs
    int clearFrom = 0, clearTo = 0; // the variables of the blocks the error left
    int scopes = 0;   // debugger: blocks open where the handler runs
};

// A compiled function, a lambda, or a program's top level.
struct Proto {
    std::string name;                  // the function's name; empty for a program
    const std::string* file = nullptr;
    int line = 0;                      // where the function is defined
    std::vector<Instr> code;
    std::vector<int> lines;            // the source line of each instruction
    ValueList constants;
    std::vector<std::pair<int, int>> preload; // constant registers: {register, constant}
    std::vector<Conversion> conversions;
    std::vector<GlobalSite> globals;
    std::vector<CallSite> calls;
    std::vector<std::int32_t> argRegisters; // for CallDirect
    std::vector<SetPath> paths;
    std::vector<FieldSite> fields;
    std::vector<Handler> handlers;
    std::vector<std::pair<int, int>> tryBodies; // [start, end) of every try block
    std::vector<Declaration*> declarations;
    std::vector<std::string> texts;    // operator texts and statement names, for messages
    std::vector<std::shared_ptr<Proto>> lambdas; // the lambdas written in this code
    std::vector<std::shared_ptr<const Callee>> callees; // what each lambda's values call
    std::vector<Capture> captures;     // a lambda's: where each captured variable comes from
    bool lambda = false;
    bool method = false; // a struct's method: parameter 0 is `this`, not counted in messages
    int registers = 0;
    int pendingErrors = 0;             // finally blocks that can hold an error
    std::vector<int> boxedParams;      // parameters a lambda captures: boxed when a call begins
    bool debug = false;                // compiled for a debugger: statement and scope events
    // A function's signature, checked by every call.
    std::vector<Conversion> params;
    Conversion result{Value::Kind::Void, "void", ""};
    // A body that only passes its parameters, in order, to one call, like the standard
    // library's `void draw_rect(int x, ...) { gfx_rect(x, ...); }`: that call's site.
    // Arguments that already have the parameters' types go there directly.
    int forward = -1;
    // The names of the slots, for the debugger and the console.
    std::shared_ptr<const std::vector<std::string>> slotNames;
    int slots = 0;
};

// Compiles a function body (numbering its variables first if needed).
std::shared_ptr<Proto> compileFunction(const FuncDefNode& function, bool debug);
// What a program's top level is compiled as: the program itself; a module's code, all
// of it or (for using and include) only its declarations. Only a program reports its
// statements to a debugger, as it always has.
enum class Unit { Program, Module, Declarations };
std::shared_ptr<Proto> compileProgram(BlockNode& program, Unit unit, bool debug);
// Code to run in a given scope and find names through it: statements typed into the
// debugger's console, or an expression (whose value the code returns). `outside` is the
// error for return, break or continue that would leave the statements.
std::shared_ptr<Proto> compileStatements(BlockNode& statements, const std::string& outside);
std::shared_ptr<Proto> compileExpression(Node& expression);
// The instructions as text, for `skrumi disasm`.
void disassemble(const Proto& proto, std::ostream& out);

} // namespace bytecode

namespace vm {
// Calls a Skrumi function with its arguments (moved from), as a call in the program would.
Value call(const FuncDefNode& function, Value* args, size_t count, Context& caller);
// Runs a program's top level (or a module's) in the root scope.
void run(BlockNode& program, Context& root, bytecode::Unit unit);
// Runs code compiled for a scope (compileStatements, compileExpression) in that scope.
Value run(bytecode::Proto& proto, Context& scope);
// Compiles and runs: an expression's value, or statements, in the scope.
Value evaluate(Node& expression, Context& scope);
void execute(BlockNode& statements, Context& scope, const std::string& outside);
} // namespace vm

} // namespace skrumi
