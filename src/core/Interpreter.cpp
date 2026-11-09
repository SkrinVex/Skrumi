#include "skrumi/Skrumi.h"
#include "skrumi/Bytecode.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <filesystem>

namespace skrumi {

namespace {

// Imports bring in declarations; calls on a module's top level are its own demo code.
void runModule(BlockNode& program, Context& ctx, bool importOnly) {
    vm::run(program, ctx, importOnly ? bytecode::Unit::Declarations : bytecode::Unit::Module);
}

// Parses a source, naming the file in a syntax error that the lexer could not name.
std::unique_ptr<BlockNode> parseSource(const std::string& text, const std::string& identity) {
    try {
        Lexer lexer(text);
        Parser parser(lexer.tokenize(), identity);
        return parser.parseProgram();
    } catch (SyntaxError& error) {
        if (error.file().empty()) error.setFile(runtime::displayPath(identity));
        throw;
    }
}

Interpreter& interpreterOf(Context& ctx) {
    if (!ctx.interpreter) throw std::runtime_error("Module Error: imports need an Interpreter");
    return *ctx.interpreter;
}

} // namespace

void executeIncludeHook(const std::string& path, Context& ctx, const std::string& currentFile, bool importOnly) {
    interpreterOf(ctx).executeInclude(path, currentFile, importOnly);
}

void executeUsingHook(const std::string& libName, Context& ctx, const std::string& currentFile, const std::string& alias) {
    interpreterOf(ctx).executeUsing(libName, currentFile, alias);
}

Interpreter::Interpreter() : Interpreter(InterpreterOptions{}) {}

Interpreter::Interpreter(InterpreterOptions opts) : options(std::move(opts)) {
    globalContext.interpreter = this;
    sources = options.sources ? options.sources : filesystemSources(options.skrumiHome);
}

Interpreter::~Interpreter() {
    // The variables go first; the rings of containers they left behind go with them.
    globalContext.variables.clear();
    globalContext.functions.clear();
    globalContext.retired.clear();
    globalContext.structs.clear();
    runtime::collectCycles();
}

std::string Interpreter::getVersion() {
    return SKRUMI_VERSION;
}

Context& Interpreter::getContext() {
    return globalContext;
}

const Context& Interpreter::getContext() const {
    return globalContext;
}

void Interpreter::reset() {
    globalContext.graphics.reset();
    globalContext.fonts.reset();
    globalContext.canvases.reset();
    globalContext.mixer.reset();
    globalContext.randomEngine.reset();
    globalContext.server.reset();
    globalContext.variables.clear();
    globalContext.nullableGlobals.clear();
    globalContext.constants.clear();
    ++globalContext.generation;
    globalContext.functions.clear();
    globalContext.retired.clear();
    ++globalContext.functionGeneration;
    globalContext.structs.clear();
    loadedModules.clear();
}

void Interpreter::setGlobal(const std::string& name, const std::string& type, const std::string& value) {
    globalContext.defineVar(name, type, runtime::parseScalar(type, value, "global variable '" + name + "'"));
}

Value Interpreter::getGlobal(const std::string& name) const {
    return globalContext.getVar(name);
}

const std::set<std::string>& Interpreter::getLoadedModules() const {
    return loadedModules;
}

void Interpreter::executeInclude(const std::string& path, const std::string& currentFile, bool importOnly) {
    executeModule(sources->resolve({path, false}, currentFile), importOnly);
}

void Interpreter::executeModule(const std::string& fullPath, bool importOnly) {
    if (loadedModules.count(fullPath)) return;
    auto program = parseSource(sources->read(fullPath), fullPath);
    loadedModules.insert(fullPath);
    auto& functions = moduleFunctions[fullPath];
    auto& variables = moduleVariables[fullPath];
    for (const auto& stmt : program->stmts) {
        if (auto* function = dynamic_cast<const FuncDefNode*>(stmt.get())) functions.push_back(function->name);
        else if (auto* variable = dynamic_cast<const VarDeclNode*>(stmt.get())) variables.push_back(variable->name);
        else if (auto* list = dynamic_cast<const ArrayDeclNode*>(stmt.get())) variables.push_back(list->name);
    }
    // The importing statement resumes after the module, so its location is restored.
    runtime::StackGuard& guard = runtime::stackGuard();
    int line = guard.line;
    const std::string* file = guard.file;
    runModule(*program, globalContext, importOnly);
    guard.line = line;
    guard.file = file;
}

void Interpreter::executeUsing(const std::string& libName, const std::string& currentFile, const std::string& alias) {
    std::string identity = sources->resolve({libName, true}, currentFile);
    executeModule(identity, true);
    if (alias.empty()) return;
    Value names = runtime::makeMap();
    Object& members = *names.ref();
    for (const auto& name : moduleFunctions[identity]) {
        auto callee = std::make_shared<Callee>();
        callee->name = name;
        callee->function = globalContext.getFunc(name);
        if (!callee->function) continue;
        Value function = Value::container(Value::Kind::Function);
        function.ref()->callee = std::move(callee);
        members.slot(name) = std::move(function);
    }
    for (const auto& name : moduleVariables[identity]) {
        auto found = globalContext.variables.find(name);
        if (found != globalContext.variables.end()) members.slot(name) = found->second;
    }
    members.frozen = true;
    members.moduleAlias = true;
    globalContext.variables[alias] = std::move(names);
    globalContext.constants.insert(alias);
    ++globalContext.generation;
}

RunResult Interpreter::runFile(const std::string& filepath) {
    if (!isSourceFile(filepath)) return {false, 1, "source files must use .skm"};
    if (options.loadDotEnv) {
        runtime::loadDotEnv(filepath);
    }

    std::ifstream file(platform::pathFromUtf8(filepath), std::ios::binary);
    if (!file.is_open()) {
        return {false, 1, "could not open file '" + filepath + "'"};
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::error_code ec;
    auto identity = platform::pathToUtf8(std::filesystem::weakly_canonical(platform::pathFromUtf8(filepath), ec));
    return runSource(buffer.str(), ec ? filepath : identity);
}

RunResult Interpreter::runSource(const std::string& source, const std::string& scriptPath) {
    runtime::StackGuard& guard = runtime::stackGuard();
    guard.line = 0;
    guard.file = nullptr;
    guard.placed = false;
    auto located = [&](const std::string& message) { return runtime::locate(message, scriptPath); };
    try {
        auto program = parseSource(source, scriptPath);
        loadedModules.insert(scriptPath);
        vm::run(*program, globalContext, bytecode::Unit::Program);
        return {true, 0, ""};
    } catch (const ExitRequest& request) {
        return {request.code == 0, request.code, ""};
    } catch (const SyntaxError& error) {
        return {false, 1, error.what()};
    } catch (const std::exception& e) {
        return {false, 1, located(e.what())};
    }
}

} // namespace skrumi
