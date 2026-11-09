#include "ProjectTools.h"
#include "ProjectLicenses.h"
#include "skrumi/Platform.h"
#include <toml++/toml.hpp>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
namespace skrumi::cli {
namespace fs = std::filesystem;
ProjectManifest manifest(const fs::path& directory) {
    ProjectManifest result;
    result.root = fs::absolute(directory);
    auto path = result.root / "skrumi.toml";
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("Project Error: cannot read " + platform::pathToUtf8(path));
    auto table = toml::parse(stream, platform::pathToUtf8(path));
    auto field = [&](const char* key, const char* fallback) {
        if (!table.contains(key)) return std::string(fallback);
        auto value = table[key].value<std::string>();
        if (!value || value->empty()) throw std::runtime_error(std::string("Project Error: '") + key + "' must be a nonempty string");
        return *value;
    };
    for (const auto& [key, value] : table) {
        (void)value;
        if (key != "name" && key != "version" && key != "entry" && key != "resources" && key != "lint")
            throw std::runtime_error("Project Error: unknown manifest key '" + std::string(key.str()) + "'");
    }
    result.name = field("name", "app"); result.version = field("version", "0.1.0");
    if (result.name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-") != std::string::npos)
        throw std::runtime_error("Project Error: name must use ASCII letters, digits, '-' or '_'");
    auto relative = [&](const std::string& value) {
        auto path = platform::pathFromUtf8(value);
        if (path.is_absolute()) throw std::runtime_error("Project Error: paths must be relative to skrumi.toml");
        for (const auto& part : path) if (part == "..") throw std::runtime_error("Project Error: paths must stay inside the project");
        return result.root / path;
    };
    result.entry = relative(field("entry", "main.skm"));
    if (result.entry.extension() != ".skm") throw std::runtime_error("Project Error: entry must use .skm");
    if (table.contains("resources")) result.resources = relative(field("resources", "resources"));
    if (table.contains("lint")) {
        auto* lint = table["lint"].as_table();
        if (!lint) throw std::runtime_error("Project Error: lint must be a table");
        for (const auto& [key, value] : *lint) {
            if (key != "deny_warnings" || !value.is_boolean()) throw std::runtime_error("Project Error: lint supports only deny_warnings = true/false");
        }
        result.denyWarnings = (*lint)["deny_warnings"].value_or(false);
    }
    return result;
}
int initProject(const std::string& directory) {
    fs::path root = platform::pathFromUtf8(directory);
    if (root.empty()) root = ".";
    const std::vector<fs::path> names = {"main.skm", "skrumi.toml", ".gitignore", "tests/smoke_test.skm"};
    for (const auto& name : names)
        if (fs::exists(fs::symlink_status(root / name))) throw std::runtime_error("Init Error: refusing to overwrite " + platform::pathToUtf8(root/name));
    std::string name = platform::pathToUtf8(fs::absolute(root).lexically_normal().filename());
    if (name.empty() || name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-") != std::string::npos) name = "app";
    fs::create_directories(root / "tests");
    toml::table table{{"name", name}, {"version", "0.1.0"}, {"entry", "main.skm"}, {"lint", toml::table{{"deny_warnings", false}}}};
    std::ostringstream config; config << table << '\n';
    std::vector<std::string> contents = {"void main() {\n    print(\"Hello, Skrumi!\");\n}\nmain();\n", config.str(), "/build/\n/.env\n", "void test_smoke() {\n    assert_equal(2 + 2, 4);\n}\n"};
    for (size_t i=0; i<names.size(); ++i) {
        std::ofstream output(root/names[i], std::ios::binary);
        output << contents[i];
        if (!output) throw std::runtime_error("Init Error: could not write " + platform::pathToUtf8(root/names[i]));
    }
    std::cout << "Initialized " << platform::pathToUtf8(root) << " (skrumi run ., skrumi check --project, skrumi test)\n";
    return 0;
}
const char* projectThirdPartyLicenses() { return projectLicenses(); }
}
