#include "skrumi/Unicode.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <vector>
static void append(std::string& text, unsigned cp) {
    if (cp < 0x80) text += char(cp);
    else if (cp < 0x800) { text += char(0xc0 | (cp >> 6)); text += char(0x80 | (cp & 63)); }
    else if (cp < 0x10000) { text += char(0xe0 | (cp >> 12)); text += char(0x80 | ((cp >> 6)&63)); text += char(0x80 | (cp&63)); }
    else { text += char(0xf0 | (cp >> 18)); text += char(0x80 | ((cp >> 12)&63)); text += char(0x80 | ((cp >> 6)&63)); text += char(0x80 | (cp&63)); }
}
int main(int argc, char** argv) {
    if (argc != 2) return 1;
    std::ifstream input(argv[1]); if (!input) return 1;
    size_t lineNumber=0, count=0;
    for (std::string line; std::getline(input,line);) {
        ++lineNumber; line = line.substr(0,line.find('#'));
        std::istringstream fields(line); std::string word, text; std::vector<size_t> expected;
        while (fields >> word) {
            if (word == "÷") expected.push_back(text.size());
            else if (word != "×") append(text, static_cast<unsigned>(std::stoul(word,nullptr,16)));
        }
        if (expected.empty()) continue;
        if (skrumi::unicode::graphemeBoundaries(text) != expected) { std::cerr << "UAX29 mismatch line " << lineNumber << '\n'; return 1; }
        ++count;
    }
    if (count < 1000) return 1;
    bool rejected=false;
    try { skrumi::unicode::graphemes(std::string("\xff",1)); } catch (...) { rejected=true; }
    if (!rejected) return 1;
    std::cout << "UNICODE_OK " << count << " conformance cases\n";
}
