#include "skrumi/Formatter.h"
#include <cctype>
#include <vector>

namespace skrumi {
namespace {

std::string trim(const std::string& text) {
    size_t first = text.find_first_not_of(" \t");
    if (first == std::string::npos) return "";
    return text.substr(first, text.find_last_not_of(" \t") - first + 1);
}

bool startsWithWord(const std::string& text, const std::string& word) {
    if (text.compare(0, word.size(), word) != 0) return false;
    if (text.size() == word.size()) return true;
    char next = text[word.size()];
    return !(std::isalnum(static_cast<unsigned char>(next)) || next == '_');
}

// The code part of a line: strings stay, a // comment is cut off.
std::string codeOf(const std::string& line, bool& blockComment) {
    std::string code;
    bool inString = false;
    for (size_t i = 0; i < line.size(); ++i) {
        char c = line[i];
        if (blockComment) {
            if (c == '*' && i + 1 < line.size() && line[i + 1] == '/') {
                blockComment = false; ++i;
            }
            code += ' ';
        } else if (inString) {
            code += c;
            if (c == '\\' && i + 1 < line.size()) code += line[++i];
            else if (c == '"') inString = false;
        } else if (c == '"') { inString = true; code += c;
        } else if (c == '/' && i + 1 < line.size() && line[i + 1] == '/') { break;
        } else if (c == '/' && i + 1 < line.size() && line[i + 1] == '*') {
            blockComment = true; ++i; code += ' ';
        } else code += c;
    }
    return trim(code);
}

struct Open {
    char bracket;
    int weight; // a switch's block holds its case labels one level above their statements
};

} // namespace

std::string formatSource(const std::string& source) {
    std::vector<std::string> lines;
    std::string current;
    for (char c : source) {
        if (c == '\n') {
            if (!current.empty() && current.back() == '\r') current.pop_back();
            lines.push_back(current);
            current.clear();
        } else {
            current += c;
        }
    }
    if (!current.empty()) {
        if (current.back() == '\r') current.pop_back();
        lines.push_back(current);
    }

    std::vector<Open> open;
    auto depth = [&] {
        int total = 0;
        for (const auto& entry : open) total += entry.weight;
        return total;
    };
    std::string out;
    int blankRun = 0;
    bool previousOpensBlock = true; // no blank lines at the start of the file
    bool continuation = false;
    bool blockComment = false;
    for (const auto& raw : lines) {
        std::string content = trim(raw);
        if (content.empty()) {
            ++blankRun;
            continue;
        }
        std::string code = codeOf(content, blockComment);
        bool closesBlock = content[0] == '}';
        if (blankRun > 0 && !previousOpensBlock && !closesBlock) out += "\n";
        blankRun = 0;

        // Closing brackets at the start of the line belong to the outer level.
        int level = depth();
        size_t leading = 0;
        std::vector<Open> probe = open;
        while (leading < code.size() && (code[leading] == '}' || code[leading] == ']' || code[leading] == ')' || code[leading] == ' ')) {
            if (code[leading] != ' ' && !probe.empty()) {
                level -= probe.back().weight;
                probe.pop_back();
            }
            ++leading;
        }
        bool label = startsWithWord(code, "case") || startsWithWord(code, "default");
        if (label && !open.empty() && open.back().weight == 2 && leading == 0) level -= 1;
        if (continuation && leading == 0) level += 1;
        if (level < 0) level = 0;
        size_t indent = static_cast<size_t>(level) * 4;
        // Inside brackets and in a continued statement, a deeper hand alignment (under
        // an opening parenthesis, nested JSON text) is the author's choice and stays.
        bool aligned = (continuation || (!open.empty() && open.back().bracket != '{')) && leading == 0;
        if (aligned) {
            size_t original = 0;
            while (original < raw.size() && (raw[original] == ' ' || raw[original] == '\t'))
                original += raw[original] == '\t' ? 4 : 1;
            if (original > indent) indent = original;
        }
        out += std::string(indent, ' ') + content + "\n";

        bool inString = false;
        for (size_t i = 0; i < code.size(); ++i) {
            char c = code[i];
            if (inString) {
                if (c == '\\') ++i;
                else if (c == '"') inString = false;
                continue;
            }
            if (c == '"') {
                inString = true;
            } else if (c == '{' || c == '[' || c == '(') {
                bool switchBlock = c == '{' && startsWithWord(code, "switch");
                open.push_back({c, switchBlock ? 2 : 1});
            } else if ((c == '}' || c == ']' || c == ')') && !open.empty()) {
                open.pop_back();
            }
        }
        char last = code.empty() ? ';' : code.back();
        previousOpensBlock = last == '{';
        // A statement that goes on to the next line, outside any brackets.
        bool insideBrackets = !open.empty() && open.back().bracket != '{';
        continuation = !code.empty() && !insideBrackets && last != ';' && last != '{' && last != '}' && last != ':' &&
                       last != ',' && !(code.size() >= 2 && code.compare(0, 2, "//") == 0);
    }
    return out;
}

} // namespace skrumi
