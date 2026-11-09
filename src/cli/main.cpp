#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include "skrumi/Skrumi.h"
#include "skrumi/SemanticAnalyzer.h"
#include "skrumi/Project.h"
#include <filesystem>
#include "Image.h"
#include "DebugAdapter.h"
#include "Help.h"
#include "Launch.h"
#include "Tools.h"
#include "ProjectTools.h"
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#endif

bool skrumiHasBundleDescriptor();
bool skrumiHasBundlePayload();

// Reports the diagnostics the editor already sees, for a terminal or CI, without executing code.
static int check(const std::string& path, bool denyWarnings = false, size_t maxErrors = 10) {
    if (!skrumi::isSourceFile(path)) throw std::runtime_error("source files must use .skm");
    std::ifstream file(skrumi::platform::pathFromUtf8(path), std::ios::binary);
    if (!file.is_open()) throw std::runtime_error("could not open file '" + path + "'");
    std::stringstream buffer;
    buffer << file.rdbuf();

    skrumi::Lexer lexer(buffer.str(), true);
    auto tokens = lexer.tokenize();
    std::vector<skrumi::Diagnostic> diagnostics = lexer.getDiagnostics();
    for (auto& d : diagnostics) if (d.code == 0) d.code = 1000;

    skrumi::Parser parser(std::move(tokens), path);
    std::vector<skrumi::Diagnostic> parserDiagnostics;
    auto program = parser.parseProgramWithDiagnostics(parserDiagnostics);
    for (auto& d : parserDiagnostics) if (d.code == 0) d.code = 1001;
    diagnostics.insert(diagnostics.end(), parserDiagnostics.begin(), parserDiagnostics.end());

    // Other files of the same program declare what this one may use: the project is
    // searched below the working directory when the file is inside it.
    std::string home = skrumi::platform::configEnv("SKRUMI_HOME");
    auto sources = std::make_shared<skrumi::OverlaySources>(skrumi::filesystemSources(home));
    skrumi::ProjectIndex project(sources);
    std::string canonical = skrumi::canonicalPath(path);
    std::string cwd = skrumi::platform::pathToUtf8(std::filesystem::current_path());
    if (canonical.rfind(cwd, 0) == 0) project.setRoot(cwd);
    skrumi::SemanticAnalyzer analyzer(canonical, home, sources);
    analyzer.addProjectFiles(project.peers(canonical));
    analyzer.analyze(program.get());
    const auto& semantic = analyzer.getDiagnostics();
    if (diagnostics.empty()) diagnostics.insert(diagnostics.end(), semantic.begin(), semantic.end());

    int errors = 0;
    size_t shown = 0;
    for (const auto& diagnostic : diagnostics) {
        if (diagnostic.severity == skrumi::DiagnosticSeverity::Error ||
            (denyWarnings && diagnostic.severity == skrumi::DiagnosticSeverity::Warning)) ++errors;
        if (shown++ >= maxErrors) continue;
        std::cout << path << ":" << diagnostic.range.start.line << ":" << diagnostic.range.start.column
                  << ": " << diagnostic.severityString() << ": ";
        if (diagnostic.code && diagnostic.message.rfind("SKR", 0) != 0) std::cout << "SKR" << diagnostic.code << ": ";
        std::cout << diagnostic.message << std::endl;
    }
    if (diagnostics.size() > maxErrors) std::cout << "shown " << maxErrors << " of " << diagnostics.size() << " diagnostics; fix the first error and check again" << std::endl;
    if (diagnostics.empty()) std::cout << path << ": no problems found" << std::endl;
    else if (!errors) std::cout << path << ": no errors found (" << diagnostics.size() << " warnings)" << std::endl;
    return errors ? 1 : 0;
}

static int report(const skrumi::RunResult& result) {
    if (!result.errorMessage.empty()) skrumi::cli::reportError(result.errorMessage);
    return result.exitCode;
}

// Runs a program and ends the process. The interpreter is not taken apart value by
// value: the system takes the whole memory back at once, which after millions of
// values is much faster. Nothing else waits for the end (files are closed by the
// builtins that open them, a window's shared memory is already released), so only
// the output is flushed.
template <class Run>
[[noreturn]] static void runAndExit(const skrumi::InterpreterOptions& options, Run run) {
    auto* interpreter = new skrumi::Interpreter(options);
    int code = report(run(*interpreter));
    std::cout.flush();
    std::cerr.flush();
    std::fflush(nullptr);
    std::_Exit(code);
}

// Program arguments as UTF-8. Windows hands main() its ANSI code page, which cannot
// hold Cyrillic, so the wide command line is converted instead.
static std::vector<std::string> argumentsFrom(int argc, char* argv[], int first) {
    std::vector<std::string> arguments;
#ifdef _WIN32
    int count = 0;
    LPWSTR* wide = CommandLineToArgvW(GetCommandLineW(), &count);
    if (wide && count == argc) {
        for (int i = first; i < count; ++i) {
            int size = WideCharToMultiByte(CP_UTF8, 0, wide[i], -1, nullptr, 0, nullptr, nullptr);
            std::string text(size > 0 ? static_cast<size_t>(size - 1) : 0, '\0');
            if (size > 1) WideCharToMultiByte(CP_UTF8, 0, wide[i], -1, text.data(), size, nullptr, nullptr);
            arguments.push_back(std::move(text));
        }
        LocalFree(wide);
        return arguments;
    }
    if (wide) LocalFree(wide);
#endif
    for (int i = first; i < argc; ++i) arguments.emplace_back(argv[i]);
    return arguments;
}

int main(int argc, char* argv[]) {
    try {
    const std::vector<std::string> args = argumentsFrom(argc, argv, 0);
    if (argc == 2 && args[1] == "--skrumi-help") {
        skrumi::cli::configureConsole(skrumi::cli::ConsoleMode::Console);
        skrumi::cli::printCommandHelp(std::cout, "run");
        return 0;
    }
    if (argc == 2 && args[1] == "--skrumi-licenses") {
        skrumi::cli::configureConsole(skrumi::cli::ConsoleMode::Console);
        std::cout << skrumi::platform::thirdPartyLicenses() << "\n" << skrumi::cli::projectThirdPartyLicenses();
        return 0;
    }
#if (defined(__linux__) || defined(_WIN32)) && (defined(__x86_64__) || defined(_M_X64))
    if (!skrumiHasBundleDescriptor()) throw std::runtime_error("Bundle Error: damaged stub descriptor");
    // Reading the executable (megabytes) is only for a built app, which carries its program.
    auto embedded = skrumiHasBundlePayload()
                        ? skrumi::bundle::unpack(skrumi::bundle::readImage(skrumi::bundle::executablePath()))
                        : std::nullopt;
    if (embedded) {
        auto sources = std::make_shared<skrumi::bundle::Sources>(std::move(*embedded));
        auto launch = skrumi::cli::parseLaunch(std::vector<std::string>(args.begin() + 1, args.end()));
        skrumi::cli::configureConsole(launch.console);
        auto options = std::move(launch.interpreter);
        options.sources = sources;
        options.loadDotEnv = false;
        runAndExit(options, [&](skrumi::Interpreter& interpreter) {
            return interpreter.runSource(sources->read(sources->entry), sources->entry);
        });
    }
#endif
    const std::string version = skrumi::Interpreter::getVersion();

    if (argc == 2 && (std::string(argv[1]) == "--version" || std::string(argv[1]) == "-v")) {
        std::cout << "Skrumi " << version << std::endl;
        return 0;
    }

    auto isHelp = [](const std::string& word) { return word == "--help" || word == "-h" || word == "help"; };
    if (argc < 2 || (argc == 2 && isHelp(argv[1]))) {
        skrumi::cli::printHelp(std::cout, version);
        return argc < 2 ? 1 : 0;
    }
    // skrumi help build; skrumi build --help (a script's own --help is the script's)
    if (argc == 3 && isHelp(argv[1])) {
        if (skrumi::cli::printCommandHelp(std::cout, argv[2])) return 0;
        throw std::runtime_error("no help on '" + std::string(argv[2]) + "'; there is help on " + skrumi::cli::commandNames());
    }
    if (argc == 3 && std::string(argv[1]) != "run" && (std::string(argv[2]) == "--help" || std::string(argv[2]) == "-h") &&
        skrumi::cli::printCommandHelp(std::cout, argv[1]))
        return 0;

    if (std::string(argv[1]) == "init") {
        if (argc > 3) throw std::runtime_error("Usage: skrumi init [directory]");
        return skrumi::cli::initProject(argc == 3 ? args[2] : ".");
    }
    if (std::string(argv[1]) == "check") {
        bool denyWarnings = false;
        size_t maxErrors = 10;
        std::vector<std::string> paths;
        for (size_t i = 2; i < args.size(); ++i) {
            if (args[i] == "--deny-warnings") denyWarnings = true;
            else if (args[i] == "--project") {
                auto project = skrumi::cli::manifest(".");
                denyWarnings = denyWarnings || project.denyWarnings;
                paths.push_back(".");
            }
            else if (args[i] == "--max-errors" && i + 1 < args.size()) {
                const auto& count = args[++i];
                if (count.empty() || count.find_first_not_of("0123456789") != std::string::npos)
                    throw std::runtime_error("--max-errors requires an integer in 1..10000");
                maxErrors = std::stoul(count);
                if (!maxErrors || maxErrors > 10000) throw std::runtime_error("--max-errors must be 1..10000");
            } else if (!args[i].empty() && args[i][0] != '-') paths.push_back(args[i]);
            else throw std::runtime_error("Usage: skrumi check [--deny-warnings] [--max-errors N] <file.skm|directory>");
        }
        if (paths.empty()) throw std::runtime_error("Usage: skrumi check <file.skm|directory|--project>");
        int status = 0;
        for (const auto& path : skrumi::cli::sources(paths)) status |= check(path, denyWarnings, maxErrors);
        return status;
    }

    if (std::string(argv[1]) == "fmt") return skrumi::cli::format(std::vector<std::string>(args.begin() + 2, args.end()));
    if (std::string(argv[1]) == "test") return skrumi::cli::test(std::vector<std::string>(args.begin() + 2, args.end()));
    if (std::string(argv[1]) == "disasm") return skrumi::cli::disassemble(std::vector<std::string>(args.begin() + 2, args.end()));

    if (std::string(argv[1]) == "debug-adapter") {
        return skrumi::debug::runAdapter(std::vector<std::string>(args.begin() + 2, args.end()));
    }

    if (std::string(argv[1]) == "build") {
        if (argc < 3 || args[2].empty() || args[2][0] == '-')
            throw std::runtime_error("Usage: skrumi build <file.skm> [-o app] [--console|--no-console]");
        std::string output, resources;
        bool noConsole = false, force = false;
        for (size_t i = 3; i < args.size(); ++i) {
            if (args[i] == "--console") noConsole = false;
            else if (args[i] == "--no-console") noConsole = true;
            else if (args[i] == "--force") force = true;
            else if (args[i] == "--resources") {
                if (++i == args.size() || args[i].empty() || args[i][0] == '-' || !resources.empty())
                    throw std::runtime_error("Build Error: --resources requires one directory");
                resources = args[i];
            }
            else if (args[i] == "-o" || args[i] == "--output") {
                if (++i == args.size() || args[i].empty() || args[i][0] == '-')
                    throw std::runtime_error("Build Error: expected output filename");
                output = args[i];
            } else throw std::runtime_error("Build Error: unknown option '" + args[i] + "'");
        }
        auto input = skrumi::platform::pathFromUtf8(args[2]);
        if (std::filesystem::is_directory(input)) {
            auto project = skrumi::cli::manifest(input);
            input = project.entry;
            if (resources.empty() && !project.resources.empty()) resources = skrumi::platform::pathToUtf8(project.resources);
            if (output.empty()) {
                std::filesystem::create_directories(project.root / "build");
                output = skrumi::platform::pathToUtf8(project.root / "build" / project.name);
            }
        }
        skrumi::bundle::build(input, skrumi::platform::pathFromUtf8(output), noConsole,
                              skrumi::platform::pathFromUtf8(resources), force);
        return 0;
    }

    auto launch = skrumi::cli::parseLaunch(std::vector<std::string>(args.begin() + (args[1] == "run" ? 2 : 1), args.end()));
    if (launch.interpreter.arguments.empty()) throw std::runtime_error("expected a script filename");
    std::string script = launch.interpreter.arguments.front();
    if (std::filesystem::is_directory(skrumi::platform::pathFromUtf8(script)))
        script = skrumi::platform::pathToUtf8(skrumi::cli::manifest(skrumi::platform::pathFromUtf8(script)).entry);
    launch.interpreter.arguments.erase(launch.interpreter.arguments.begin());
    skrumi::cli::configureConsole(launch.console);
    runAndExit(launch.interpreter, [&](skrumi::Interpreter& interpreter) { return interpreter.runFile(script); });
    } catch (const std::exception& error) {
        skrumi::cli::reportError(error.what());
        return 1;
    }
}
