// skrumi fmt, skrumi test and skrumi disasm: source layout, the built-in test runner
// and the bytecode listing.
#include "Tools.h"
#include "skrumi/SourceProvider.h"
#include "skrumi/Skrumi.h"
#include "skrumi/Formatter.h"
#include "skrumi/Bytecode.h"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

namespace skrumi::cli {
namespace {

namespace fs = std::filesystem;

std::string readText(const fs::path& path) {
    if (!isSourceFile(platform::pathToUtf8(path))) throw std::runtime_error("source files must use .skm");
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("could not open file '" + platform::pathToUtf8(path) + "'");
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

// A directory stands for the .skm files below it, without hidden folders, build
// output and node_modules; a file stands for itself.
std::vector<fs::path> sourceFiles(const std::vector<std::string>& arguments, bool (*wanted)(const fs::path&)) {
    std::vector<fs::path> files;
    std::vector<std::string> roots = arguments.empty() ? std::vector<std::string>{"."} : arguments;
    for (const auto& root : roots) {
        fs::path path = platform::pathFromUtf8(root);
        std::error_code error;
        if (fs::is_regular_file(path, error)) {
            if (!isSourceFile(root)) throw std::runtime_error("source files must use .skm");
            files.push_back(path);
            continue;
        }
        if (!fs::is_directory(path, error)) throw std::runtime_error("no such file or directory: " + root);
        fs::recursive_directory_iterator it(path, fs::directory_options::skip_permission_denied, error), end;
        for (; it != end; it.increment(error)) {
            if (error) break;
            std::string name = platform::pathToUtf8(it->path().filename());
            if (it->is_directory(error)) {
                if (name.size() > 1 && (name[0] == '.' || name.rfind("build", 0) == 0 || name == "node_modules"))
                    it.disable_recursion_pending();
                continue;
            }
            if (isSourceFile(platform::pathToUtf8(it->path())) && wanted(it->path())) files.push_back(it->path());
        }
    }
    std::sort(files.begin(), files.end());
    return files;
}

bool anySource(const fs::path&) { return true; }

bool testFile(const fs::path& path) {
    std::string stem = platform::pathToUtf8(path.stem());
    return stem.size() > 5 && stem.compare(stem.size() - 5, 5, "_test") == 0;
}

// Names of the test_ functions of a file with no parameters, in the order written.
std::vector<std::string> testNames(const std::string& source, const std::string& file) {
    Lexer lexer(source);
    Parser parser(lexer.tokenize(), file);
    auto program = parser.parseProgram();
    std::vector<std::string> names;
    for (const auto& stmt : program->stmts) {
        auto* function = dynamic_cast<const FuncDefNode*>(stmt.get());
        if (function && function->name.rfind("test_", 0) == 0 && function->params.empty()) names.push_back(function->name);
    }
    return names;
}

} // namespace

std::vector<std::string> sources(const std::vector<std::string>& paths) {
    std::vector<std::string> out;
    for (const auto& file : sourceFiles(paths, anySource)) out.push_back(platform::pathToUtf8(file));
    if (out.empty()) throw std::runtime_error("no .skm source files found");
    return out;
}

int format(const std::vector<std::string>& arguments) {
    bool check = false;
    std::vector<std::string> paths;
    for (const auto& argument : arguments) {
        if (argument == "--check") check = true;
        else if (!argument.empty() && argument[0] == '-') throw std::runtime_error("Usage: skrumi fmt [--check] [files or directories...]");
        else paths.push_back(argument);
    }
    int changed = 0;
    for (const auto& path : sourceFiles(paths, anySource)) {
        std::string text = readText(path);
        std::string formatted = formatSource(text);
        if (formatted == text) continue;
        ++changed;
        std::string shown = platform::pathToUtf8(path.lexically_normal());
        if (check) {
            std::cout << shown << ": not formatted" << std::endl;
            continue;
        }
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out << formatted;
        if (!out) throw std::runtime_error("could not write '" + shown + "'");
        std::cout << "formatted " << shown << std::endl;
    }
    if (check && changed == 0) std::cout << "all files are formatted" << std::endl;
    return check && changed > 0 ? 1 : 0;
}

int test(const std::vector<std::string>& arguments) {
    std::string filter;
    std::vector<std::string> paths;
    for (size_t i = 0; i < arguments.size(); ++i) {
        if (arguments[i] == "--filter" && i + 1 < arguments.size()) filter = arguments[++i];
        else if (!arguments[i].empty() && arguments[i][0] == '-') throw std::runtime_error("Usage: skrumi test [--filter text] [files or directories...]");
        else paths.push_back(arguments[i]);
    }
    int passed = 0, failed = 0;
    for (const auto& path : sourceFiles(paths, testFile)) {
        std::string file = platform::pathToUtf8(path.lexically_normal());
        std::cout << file << std::endl;
        std::vector<std::string> names;
        try {
            names = testNames(readText(path), file);
        } catch (const std::exception& error) {
            std::cout << "  FAIL (file): " << error.what() << std::endl;
            ++failed;
            continue;
        }
        size_t ran = 0;
        if (names.empty() && filter.empty()) {
            Interpreter interpreter;
            auto result = interpreter.runFile(file);
            if (result.success && result.exitCode == 0) { ++passed; std::cout << "  ok   (script)" << std::endl; }
            else { ++failed; std::cout << "  FAIL (script): " << result.errorMessage << std::endl; }
            continue;
        }
        for (const auto& name : names) {
            if (!filter.empty() && name.find(filter) == std::string::npos) continue;
            ++ran;
            // Each test starts from a fresh program: one test's globals never leak into the next.
            Interpreter interpreter;
            auto started = std::chrono::steady_clock::now();
            RunResult setup = interpreter.runFile(file);
            std::string failure = setup.success ? "" : setup.errorMessage;
            if (setup.success) {
                try {
                    auto function = interpreter.getContext().getFunc(name);
                    static_cast<const FuncDefNode*>(function.get())->invoke({}, interpreter.getContext());
                } catch (const ExitRequest& request) {
                    if (request.code != 0) failure = "exit(" + std::to_string(request.code) + ")";
                } catch (const std::exception& error) {
                    failure = runtime::locate(error.what(), file);
                }
            }
            double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
            std::ostringstream time;
            time.precision(1);
            time << std::fixed << ms;
            if (failure.empty()) {
                ++passed;
                std::cout << "  ok   " << name << " (" << time.str() << " ms)" << std::endl;
            } else {
                ++failed;
                std::cout << "  FAIL " << name << ": " << failure << std::endl;
            }
        }
        if (ran == 0) std::cout << "  no tests" << (filter.empty() ? "" : " matching '" + filter + "'") << std::endl;
    }
    std::cout << (passed + failed) << " tests: " << passed << " passed, " << failed << " failed" << std::endl;
    return failed > 0 ? 1 : 0;
}

namespace {

// The functions defined anywhere in the code, in source order.
void collectFunctions(Node* node, std::vector<FuncDefNode*>& out) {
    if (!node) return;
    if (auto* n = dynamic_cast<FuncDefNode*>(node)) {
        out.push_back(n);
        collectFunctions(n->body.get(), out);
    } else if (auto* n = dynamic_cast<BlockNode*>(node)) {
        for (auto& stmt : n->stmts) collectFunctions(stmt.get(), out);
    } else if (auto* n = dynamic_cast<IfNode*>(node)) {
        collectFunctions(n->thenB.get(), out);
        collectFunctions(n->elseB.get(), out);
    } else if (auto* n = dynamic_cast<WhileNode*>(node)) {
        collectFunctions(n->body.get(), out);
    } else if (auto* n = dynamic_cast<ForNode*>(node)) {
        collectFunctions(n->body.get(), out);
    } else if (auto* n = dynamic_cast<SwitchNode*>(node)) {
        for (auto& item : n->cases) collectFunctions(item.second.get(), out);
        collectFunctions(n->defaultCase.get(), out);
    } else if (auto* n = dynamic_cast<TryNode*>(node)) {
        collectFunctions(n->body.get(), out);
        collectFunctions(n->handler.get(), out);
        collectFunctions(n->cleanup.get(), out);
    }
}

} // namespace

int disassemble(const std::vector<std::string>& arguments) {
    if (arguments.size() != 1) throw std::runtime_error("Usage: skrumi disasm <file.skm>");
    fs::path path = platform::pathFromUtf8(arguments[0]);
    Lexer lexer(readText(path));
    Parser parser(lexer.tokenize(), arguments[0]);
    auto program = parser.parseProgram();
    bytecode::disassemble(*bytecode::compileProgram(*program, bytecode::Unit::Program, false), std::cout);
    std::vector<FuncDefNode*> functions;
    collectFunctions(program.get(), functions);
    for (FuncDefNode* function : functions) {
        std::cout << "\n";
        bytecode::disassemble(*bytecode::compileFunction(*function, false), std::cout);
    }
    return 0;
}

} // namespace skrumi::cli
