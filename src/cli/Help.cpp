#include "Help.h"
#include <map>

namespace skrumi::cli {
namespace {

// What `skrumi help <command>` and `skrumi <command> --help` show.
const std::map<std::string, const char*>& commands() {
    static const std::map<std::string, const char*> text = {
        {"run",
         "Usage: skrumi [runtime options] <script.skm> [args...]\n"
         "       skrumi run <script.skm|project-directory> [args...]\n"
         "       app [runtime options] [args...]\n"
         "\n"
         "Runs a program. User arguments reach os_args() unchanged. Reserved options\n"
         "may also follow the script name; -- stops runtime option parsing.\n"
         "\n"
         "  --skrumi-console       Attach/create a Windows console\n"
         "  --skrumi-no-console    Detach this process from its Windows console\n"
         "  --skrumi-width N       Initial drawing width, 64..4096 pixels\n"
         "  --skrumi-height N      Initial drawing height, 64..4096 pixels\n"
         "  --skrumi-fullscreen    Open graphics windows fullscreen\n"
         "  --skrumi-windowed      Open graphics windows in windowed mode (default)\n"
         "\n"
         "Last option wins. Width * height must not exceed 8388608 pixels; an omitted\n"
         "dimension comes from open_window. Later graphics API calls can change it.\n"
         "Console options are no-ops on Linux, which never creates an extra terminal.\n"
         "A Windows console EXE may briefly show its console before detaching; build\n"
         "with --no-console to avoid this. Redirection to files/pipes is preserved.\n"
         "Fullscreen needs Win32 or an EWMH-compatible X11 window manager.\n"
         "A standalone app accepts --skrumi-help or --skrumi-licenses alone for\n"
         "runtime help/licenses. Its own --help and --version remain user arguments.\n"
         "A .env next to a script (or in cwd) supplies unset environment variables;\n"
         "standalone programs do not load .env automatically.\n"
         "\n"
         "Examples:\n"
         "  skrumi --skrumi-width 960 --skrumi-height 540 game.skm --level 2\n"
         "  game.exe --skrumi-no-console --skrumi-fullscreen\n"
         "  game.exe --skrumi-console --skrumi-windowed -- --skrumi-width custom\n"},
        {"init", "Usage: skrumi init [directory]\nCreates main.skm, skrumi.toml, .gitignore and tests/smoke_test.skm. Never overwrites existing files.\n"},
        {"check",
         "Usage: skrumi check <script.skm|directory> [--deny-warnings] [--max-errors N]\n"
         "       skrumi check --project [--deny-warnings]\n"
         "\n"
         "Reports syntax errors, type errors and unknown names without running the\n"
         "program. Exits with 1 when there is an error.\n"
         "\n"
         "Example:\n"
         "  skrumi check app.skm\n"},
        {"build",
         "Usage: skrumi build <file.skm|project-directory> [-o|--output name] [--force] [--console|--no-console]\n"
         "\n"
         "Builds one standalone executable for this OS and CPU: the interpreter, the\n"
         "program and every module it uses (using, include) in a single file. The people\n"
         "who run it need neither Skrumi nor a compiler.\n"
         "\n"
         "  -o, --output name   Output file; the script's name by default. On Windows\n"
         "                      .exe is added. An existing file requires --force.\n"
         "\n"
         "  --force            Atomically replace an existing regular output file\n"
         "  --console          Windows console EXE (default)\n"
         "  --no-console       Windows GUI EXE: no console is created at startup\n"
         "                      On Linux these switches have no effect. Last wins.\n"
         "  --resources dir    Embed regular files from this explicit directory.\n"
         "                      Read with resource_read; links are rejected.\n"
         "\n"
         "Override at launch with --skrumi-console / --skrumi-no-console.\n"
         "See skrumi help run for window size, fullscreen and user arguments.\n"
         "\n"
         "Not inside the file: .env, environment variables and resources the program\n"
         "reads with read_file; pass secrets through the environment at run time. A Linux\n"
         "build makes a Linux program, a Windows build an .exe; there is no cross-building.\n"
         "The sources are stored inside and can be extracted: this is not code protection.\n"
         "\n"
         "Examples:\n"
         "  skrumi build game.skm --no-console makes ./game (game.exe on Windows)\n"
         "  skrumi build server.skm -o api     makes ./api\n"},
        {"test",
         "Usage: skrumi test [--filter text] [files or directories...]\n"
         "\n"
         "Runs every parameterless test_ function of the *_test.skm files found (in the\n"
         "current directory by default), each in a fresh program. Exits with 1 when a\n"
         "test fails.\n"
         "\n"
         "  --filter text   Only tests whose name contains the text\n"
         "\n"
         "Examples:\n"
         "  skrumi test\n"
         "  skrumi test tests --filter parse\n"},
        {"fmt",
         "Usage: skrumi fmt [--check] [files or directories...]\n"
         "\n"
         "Lays out .skm files: indentation, blank lines and spaces (the current directory\n"
         "by default).\n"
         "\n"
         "  --check   Change nothing; list the files that would change and exit with 1\n"
         "            when there are any (for CI)\n"
         "\n"
         "Examples:\n"
         "  skrumi fmt\n"
         "  skrumi fmt --check src\n"},
        {"disasm",
         "Usage: skrumi disasm <script.skm>\n"
         "\n"
         "Prints the bytecode the program and each of its functions run as: source line,\n"
         "instruction number, instruction and its registers.\n"},
        {"debug-adapter",
         "Usage: skrumi debug-adapter [--connect host:port]\n"
         "\n"
         "Debug Adapter Protocol server for editors (VS Code, Kate, Zed): breakpoints,\n"
         "stepping, variables. Talks over stdin/stdout; with --connect it connects to\n"
         "the editor over TCP and the program keeps its own terminal.\n"},
    };
    return text;
}

} // namespace

void printHelp(std::ostream& out, const std::string& version) {
    out << "Skrumi " << version << " - scripts, console tools, servers and 2D games\n"
        << "\n"
        << "Source files: .skm. Configuration: --skrumi-* and SKRUMI_*.\n\n"
        << "Usage: skrumi [runtime options] <script.skm> [args...]\n"
        << "       skrumi <command> [options]\n"
        << "\n"
        << "Run and check:\n"
        << "  skrumi <script.skm> [args...]     Run a program; args reach os_args()\n"
        << "  skrumi check <script.skm>         Find errors without running the program\n"
        << "  skrumi test [--filter text] [paths...]\n"
        << "                                     Run the test_ functions of *_test.skm files\n"
        << "\n"
        << "Build:\n"
        << "  skrumi build <file.skm> [-o name] One standalone executable for this OS and CPU:\n"
        << "                                     runtime, program and modules in one file\n"
        << "\n"
        << "  --no-console / --console          Windows GUI / console build mode\n"
        << "\n"
        << "Runtime options (CLI and built programs; see skrumi help run):\n"
        << "  --skrumi-console / --skrumi-no-console\n"
        << "  --skrumi-width N / --skrumi-height N\n"
        << "  --skrumi-fullscreen / --skrumi-windowed\n"
        << "  --                                Pass remaining arguments literally\n"
        << "\n"
        << "Tools:\n"
        << "  skrumi fmt [--check] [paths...]   Lay out .skm files\n"
        << "  skrumi disasm <script.skm>        Print the bytecode the program runs as\n"
        << "  skrumi debug-adapter [--connect host:port]\n"
        << "                                     Debugger for editors (VS Code, Kate, Zed)\n"
        << "\n"
        << "Information:\n"
        << "  skrumi help [command]             This help, or the details of one command\n"
        << "  skrumi --version                  Version\n"
        << "  skrumi --skrumi-help             Runtime options (also in built apps)\n"
        << "  skrumi --skrumi-licenses         Licenses of the built-in libraries\n"
        << "\n"
        << "Examples:\n"
        << "  skrumi hello.skm\n"
        << "  skrumi build game.skm             makes ./game (game.exe on Windows)\n"
        << "  skrumi help build\n"
        << "\n"
        << "Environment:\n"
        << "  SKRUMI_HOME        Installation and standard library path\n"
        << "  SKRUMI_LOG_LEVEL   debug | info | warn | error | off\n"
        << "  SKRUMI_CA_BUNDLE   PEM CA file | embedded (default) | system\n"
        << "  SKRUMI_X11_SHM     0 turns off shared-memory frames on X11\n"
        << "\n"
        << "Documentation:\n"
        << "  https://github.com/SkrinVex/Skrumi/blob/master/DOCUMENTATION.md\n"
        << "  Android SDK / APK: https://github.com/SkrinVex/Skrumi/blob/master/docs/ANDROID.md\n";
}

bool printCommandHelp(std::ostream& out, const std::string& command) {
    auto found = commands().find(command);
    if (found == commands().end()) return false;
    out << found->second;
    return true;
}

std::string commandNames() {
    std::string names;
    for (const auto& [name, text] : commands()) names += (names.empty() ? "" : ", ") + name;
    return names;
}

} // namespace skrumi::cli
