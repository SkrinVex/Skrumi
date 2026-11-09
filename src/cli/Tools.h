#pragma once
#include <string>
#include <vector>

namespace skrumi::cli {

// skrumi fmt [--check] [paths...]: lays out .skm files (see formatSource); --check
// only lists the files that would change and fails when there are any.
std::vector<std::string> sources(const std::vector<std::string>& paths);

int format(const std::vector<std::string>& arguments);

// skrumi test [--filter text] [paths...]: runs the parameterless test_ functions of
// every *_test.skm file, each in a fresh program; fails when any test fails.
int test(const std::vector<std::string>& arguments);

// skrumi disasm <file.skm>: prints the bytecode of the program and of every function
// defined in it, in the order they appear in the file.
int disassemble(const std::vector<std::string>& arguments);

} // namespace skrumi::cli
