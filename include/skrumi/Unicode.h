#pragma once
#include <string>
#include <vector>
#include <cstdint>
namespace skrumi::unicode {
// Byte offsets of all extended grapheme boundaries, including 0 and text.size().
// Invalid UTF-8 is rejected rather than silently dropping or splitting bytes.
std::vector<size_t> graphemeBoundaries(const std::string& text);
std::vector<std::string> graphemes(const std::string& text);
char32_t next(const std::string& text, size_t& offset);
}
