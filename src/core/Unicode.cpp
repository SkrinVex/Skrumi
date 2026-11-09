#include "skrumi/Unicode.h"
#include <utf8proc.h>
#include <stdexcept>
namespace skrumi::unicode {
char32_t next(const std::string& text, size_t& offset) {
    utf8proc_int32_t cp;
    auto count = utf8proc_iterate(reinterpret_cast<const utf8proc_uint8_t*>(text.data() + offset),
                                static_cast<utf8proc_ssize_t>(text.size() - offset), &cp);
    if (count <= 0) throw std::runtime_error("Unicode Error: invalid UTF-8 at byte " + std::to_string(offset));
    offset += static_cast<size_t>(count);
    return static_cast<char32_t>(cp);
}
std::vector<size_t> graphemeBoundaries(const std::string& text) {
    std::vector<size_t> result{0};
    if (text.empty()) return result;
    size_t offset = 0;
    auto previous = next(text, offset);
    utf8proc_int32_t state = 0;
    while (offset < text.size()) {
        size_t boundary = offset;
        auto current = next(text, offset);
        if (utf8proc_grapheme_break_stateful(previous, current, &state)) result.push_back(boundary);
        previous = current;
    }
    result.push_back(text.size());
    return result;
}
std::vector<std::string> graphemes(const std::string& text) {
    auto boundaries = graphemeBoundaries(text);
    std::vector<std::string> result;
    result.reserve(boundaries.size() - 1);
    for (size_t i = 1; i < boundaries.size(); ++i)
        result.push_back(text.substr(boundaries[i-1], boundaries[i] - boundaries[i-1]));
    return result;
}
}
