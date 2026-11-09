#include "Graphics.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <unordered_map>

namespace skrumi::graphics {
namespace {
// Seven rows above the baseline and two for descenders (g, p, q, y, j).
using Glyph = std::array<uint8_t, 9>;
// Original 5x7 bitmap alphabet; no OS fonts, font files or third-party font license.
// Lower-case letters have their own shapes; x-height is five rows, descenders are folded
// into the seven-row cell. Cyrillic letters that look like Latin ones share them.
const std::unordered_map<char32_t, Glyph>& lowercase() {
    static const std::unordered_map<char32_t, Glyph> letters = {
        {U'a',{0,0,14,1,15,17,15}}, {U'b',{16,16,22,25,17,17,30}}, {U'c',{0,0,14,16,16,17,14}},
        {U'd',{1,1,13,19,17,17,15}}, {U'e',{0,0,14,17,31,16,14}}, {U'f',{6,9,8,28,8,8,8}},
        {U'g',{0,0,15,17,17,17,15,1,14}}, {U'h',{16,16,22,25,17,17,17}}, {U'i',{4,0,12,4,4,4,14}},
        {U'j',{2,0,6,2,2,2,2,18,12}}, {U'k',{16,16,18,20,24,20,18}}, {U'l',{12,4,4,4,4,4,14}},
        {U'm',{0,0,26,21,21,17,17}}, {U'n',{0,0,22,25,17,17,17}}, {U'o',{0,0,14,17,17,17,14}},
        {U'p',{0,0,30,17,17,17,30,16,16}}, {U'q',{0,0,15,17,17,17,15,1,1}}, {U'r',{0,0,22,25,16,16,16}},
        {U's',{0,0,15,16,14,1,30}}, {U't',{8,8,28,8,8,9,6}}, {U'u',{0,0,17,17,17,19,13}},
        {U'v',{0,0,17,17,17,10,4}}, {U'w',{0,0,17,17,21,21,10}}, {U'x',{0,0,17,10,4,10,17}},
        {U'y',{0,0,17,17,17,17,15,1,14}}, {U'z',{0,0,31,2,4,8,31}},
        {U'б',{15,16,30,17,17,17,14}}, {U'в',{0,0,30,17,30,17,30}}, {U'г',{0,0,31,16,16,16,16}},
        {U'д',{0,0,14,10,10,10,31,17}}, {U'ё',{10,0,14,17,31,16,14}}, {U'ж',{0,0,21,21,14,21,21}},
        {U'з',{0,0,30,1,6,1,30}}, {U'и',{0,0,17,19,21,25,17}}, {U'й',{10,4,17,19,21,25,17}},
        {U'к',{0,0,18,20,24,20,18}}, {U'л',{0,0,7,9,9,9,17}}, {U'м',{0,0,17,27,21,17,17}},
        {U'н',{0,0,17,17,31,17,17}}, {U'п',{0,0,31,17,17,17,17}}, {U'т',{0,0,31,4,4,4,4}},
        {U'ф',{0,4,14,21,21,14,4}}, {U'ц',{0,0,18,18,18,18,31,1}}, {U'ч',{0,0,17,17,15,1,1}},
        {U'ш',{0,0,21,21,21,21,31}}, {U'щ',{0,0,21,21,21,21,31,1}}, {U'ъ',{0,0,24,8,14,9,14}},
        {U'ы',{0,0,17,17,29,21,29}}, {U'ь',{0,0,16,16,30,17,30}}, {U'э',{0,0,14,1,7,1,14}},
        {U'ю',{0,0,18,21,29,21,18}}, {U'я',{0,0,15,17,15,9,17}}
    };
    return letters;
}

Glyph glyph(char32_t code, bool* supported = nullptr) {
    if (supported) *supported = true;
    static const std::u32string lookalikes = U"аеорсух";
    static const std::u32string latinLower = U"aeopcyx";
    auto same = lookalikes.find(code);
    if (same != std::u32string::npos) code = latinLower[same];
    const auto& small = lowercase();
    auto lower = small.find(code);
    if (lower != small.end()) return lower->second;
    if (code >= U'a' && code <= U'z') code -= 32;
    if (code >= U'а' && code <= U'я') code -= 32;
    if (code == U'ё') code = U'Ё';
    static const std::unordered_map<char32_t, Glyph> letters = {
        {U'A',{14,17,17,31,17,17,17}}, {U'B',{30,17,17,30,17,17,30}},
        {U'C',{14,17,16,16,16,17,14}}, {U'D',{30,17,17,17,17,17,30}},
        {U'E',{31,16,16,30,16,16,31}}, {U'F',{31,16,16,30,16,16,16}},
        {U'G',{14,17,16,23,17,17,15}}, {U'H',{17,17,17,31,17,17,17}},
        {U'I',{14,4,4,4,4,4,14}}, {U'J',{7,2,2,2,18,18,12}},
        {U'K',{17,18,20,24,20,18,17}}, {U'L',{16,16,16,16,16,16,31}},
        {U'M',{17,27,21,21,17,17,17}}, {U'N',{17,25,21,19,17,17,17}},
        {U'O',{14,17,17,17,17,17,14}}, {U'P',{30,17,17,30,16,16,16}},
        {U'Q',{14,17,17,17,21,18,13}}, {U'R',{30,17,17,30,20,18,17}},
        {U'S',{15,16,16,14,1,1,30}}, {U'T',{31,4,4,4,4,4,4}},
        {U'U',{17,17,17,17,17,17,14}}, {U'V',{17,17,17,17,17,10,4}},
        {U'W',{17,17,17,21,21,27,17}}, {U'X',{17,17,10,4,10,17,17}},
        {U'Y',{17,17,10,4,4,4,4}}, {U'Z',{31,1,2,4,8,16,31}},
        {U'0',{14,17,19,21,25,17,14}}, {U'1',{4,12,4,4,4,4,14}},
        {U'2',{14,17,1,2,4,8,31}}, {U'3',{30,1,1,14,1,1,30}},
        {U'4',{2,6,10,18,31,2,2}}, {U'5',{31,16,16,30,1,1,30}},
        {U'6',{14,16,16,30,17,17,14}}, {U'7',{31,1,2,4,8,8,8}},
        {U'8',{14,17,17,14,17,17,14}}, {U'9',{14,17,17,15,1,1,14}},
        {U' ',{0,0,0,0,0,0,0}}, {U'.',{0,0,0,0,0,6,6}},
        {U',',{0,0,0,0,6,6,4}}, {U':',{0,6,6,0,6,6,0}},
        {U';',{0,6,6,0,6,6,4}}, {U'!',{4,4,4,4,4,0,4}},
        {U'?',{14,17,1,2,4,0,4}}, {U'-',{0,0,0,31,0,0,0}},
        {U'+',{0,4,4,31,4,4,0}}, {U'/',{1,1,2,4,8,16,16}},
        {U'=',{0,0,31,0,31,0,0}}, {U'(',{2,4,8,8,8,4,2}},
        {U')',{8,4,2,2,2,4,8}}, {U'[',{14,8,8,8,8,8,14}},
        {U']',{14,2,2,2,2,2,14}}, {U'_', {0,0,0,0,0,0,31}},
        {U'•',{0,0,4,14,4,0,0}}, {U'★',{4,4,31,14,14,17,0}},
        {U'⚡',{2,6,12,31,6,12,8}}, {U'❤',{0,10,31,31,14,4,0}},
        {U'☠',{14,21,31,14,0,10,21}}, {U'⬆',{4,14,31,4,4,4,0}}, {U'⬇',{0,4,4,4,31,14,4}},
        {U'♥',{0,10,31,31,14,4,0}}, {U'←',{0,4,8,31,8,4,0}},
        {U'→',{0,4,2,31,2,4,0}}, {U'Б',{31,16,16,30,17,17,30}},
        {U'Г',{31,16,16,16,16,16,16}}, {U'Д',{7,9,9,9,17,31,17}},
        {U'Ё',{10,0,31,16,30,16,31}}, {U'Ж',{21,21,14,4,14,21,21}},
        {U'З',{30,1,1,14,1,1,30}}, {U'И',{17,17,19,21,25,17,17}},
        {U'Й',{10,4,17,19,21,25,17}}, {U'Л',{7,9,9,9,9,9,17}},
        {U'П',{31,17,17,17,17,17,17}}, {U'У',{17,17,17,15,1,17,14}},
        {U'Ф',{4,14,21,21,21,14,4}}, {U'Ц',{18,18,18,18,18,31,1}},
        {U'Ч',{17,17,17,15,1,1,1}}, {U'Ш',{21,21,21,21,21,21,31}},
        {U'Щ',{21,21,21,21,21,31,1}}, {U'Ъ',{24,8,8,14,9,9,14}},
        {U'Ы',{17,17,17,29,21,21,29}}, {U'Ь',{16,16,16,30,17,17,30}},
        {U'Э',{14,17,1,7,1,17,14}}, {U'Ю',{18,21,21,29,21,21,18}},
        {U'Я',{15,17,17,15,5,9,17}},
        {U'"',{10,10,0,0,0,0,0}}, {U'\'',{4,4,0,0,0,0,0}}, {U'`',{8,4,0,0,0,0,0}},
        {U'%',{24,25,2,4,8,19,3}}, {U'*',{0,4,21,14,21,4,0}}, {U'#',{10,10,31,10,31,10,10}},
        {U'<',{2,4,8,16,8,4,2}}, {U'>',{8,4,2,1,2,4,8}}, {U'@',{14,17,23,21,23,16,14}},
        {U'&',{12,18,20,8,21,18,13}}, {U'{',{2,4,4,8,4,4,2}}, {U'}',{8,4,4,2,4,4,8}},
        {U'|',{4,4,4,4,4,4,4}}, {U'$',{4,15,20,14,5,30,4}}, {U'^',{4,10,17,0,0,0,0}},
        {U'~',{0,0,8,21,2,0,0}}, {U'\\',{16,16,8,4,2,1,1}}, {U'°',{6,9,9,6,0,0,0}},
        {U'№',{18,26,26,22,22,18,18}}, {U'«',{0,5,10,20,10,5,0}}, {U'»',{0,20,10,5,10,20,0}},
        {U'—',{0,0,0,31,0,0,0}}, {U'–',{0,0,0,14,0,0,0}}, {U'×',{0,17,10,4,10,17,0}},
        {U'…',{0,0,0,0,0,0,21}}, {U'↑',{4,14,21,4,4,4,4}}, {U'↓',{4,4,4,4,21,14,4}}
    };
    static const std::u32string russian = U"АВЕКМНОРСТХ";
    static const std::u32string latin = U"ABEKMHOPCTX";
    auto equivalent = russian.find(code);
    if (equivalent != std::u32string::npos) code = latin[equivalent];
    auto found = letters.find(code);
    if (supported) *supported = found != letters.end();
    return found == letters.end() ? letters.at(U'?') : found->second;
}
char32_t nextCode(const std::string& text, size_t& i) {
    auto first = static_cast<unsigned char>(text[i++]);
    if (first < 128) return first;
    int count = first >= 0xf0 && first <= 0xf4 ? 3 : first >= 0xe0 && first <= 0xef ? 2 : first >= 0xc2 && first <= 0xdf ? 1 : 0;
    if (!count || i + count > text.size()) return U'?';
    char32_t code = first & ((1 << (6 - count)) - 1);
    for (int n = 0; n < count; ++n) {
        auto c = static_cast<unsigned char>(text[i]);
        if ((c & 0xc0) != 0x80) return U'?';
        ++i;
        code = (code << 6) | (c & 63);
    }
    if (code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff) ||
        (count == 1 && code < 128) || (count == 2 && code < 2048) || (count == 3 && code < 65536)) return U'?';
    return code;
}
}

bool Surface::hasGlyph(char32_t cp) {
    if (cp == U'\n') return true;
    bool supported = false;
    glyph(cp, &supported);
    return supported;
}

Surface::Surface(int width, int height) : width_(width), height_(height) {
    if (width < 1 || height < 1 || width > 4096 || height > 4096 || int64_t(width) * height > 8388608)
        throw std::runtime_error("Graphics Error: dimensions must be 1..4096, at most 8388608 pixels");
    pixels_.resize(size_t(width) * height);
}
void Surface::pushClip(int x, int y, int width, int height) {
    if (width < 0 || height < 0) throw std::runtime_error("Graphics Error: clip size must not be negative");
    if (clips_.size() >= 256) throw std::runtime_error("Graphics Error: more than 256 nested clip rectangles");
    Clip outer = clip();
    int left = static_cast<int>(std::clamp<int64_t>(x, outer.left, outer.right));
    int top = static_cast<int>(std::clamp<int64_t>(y, outer.top, outer.bottom));
    int right = static_cast<int>(std::clamp<int64_t>(int64_t(x) + width, left, outer.right));
    int bottom = static_cast<int>(std::clamp<int64_t>(int64_t(y) + height, top, outer.bottom));
    clips_.push_back({left, top, right, bottom});
}
void Surface::popClip() {
    if (clips_.empty()) throw std::runtime_error("Graphics Error: clip_end without a matching clip_begin");
    clips_.pop_back();
}
void Surface::resize(int width, int height) {
    Surface next(width, height);
    for (int y = 0; y < std::min(height_, height); ++y)
        std::copy_n(pixels_.begin() + size_t(y) * width_, std::min(width_, width),
                    next.pixels_.begin() + size_t(y) * width);
    *this = std::move(next);
}

void Surface::clear(uint32_t color) { std::fill(pixels_.begin(), pixels_.end(), color & 0xffffff); }
void Surface::rectangle(int x, int y, int width, int height, uint32_t color) {
    if (width < 0 || height < 0) throw std::runtime_error("Graphics Error: rectangle size must not be negative");
    Clip area = clip();
    int left = std::max(area.left, x), top = std::max(area.top, y);
    int right = static_cast<int>(std::min<int64_t>(area.right, int64_t(x) + width));
    int bottom = static_cast<int>(std::min<int64_t>(area.bottom, int64_t(y) + height));
    if (left >= right || top >= bottom) return;
    for (int row = top; row < bottom; ++row)
        std::fill(pixels_.begin() + size_t(row) * width_ + left, pixels_.begin() + size_t(row) * width_ + right, color & 0xffffff);
}
void Surface::circle(int x, int y, int radius, uint32_t color, int alpha) {
    if (alpha < 0 || alpha > 255) throw std::runtime_error("Graphics Error: alpha must be 0..255");
    if (radius < 0 || radius > 8192) throw std::runtime_error("Graphics Error: radius must be 0..8192");
    int64_t top = std::max<int64_t>(0, int64_t(y) - radius), bottom = std::min<int64_t>(height_ - 1, int64_t(y) + radius);
    for (int64_t row = top; row <= bottom; ++row) {
        int64_t dy = row - y;
        int64_t dx = static_cast<int64_t>(std::sqrt(double(int64_t(radius) * radius - dy * dy)));
        int64_t left = std::max<int64_t>(0, int64_t(x) - dx), right = std::min<int64_t>(width_ - 1, int64_t(x) + dx);
        if (left <= right) blend(static_cast<int>(left), static_cast<int>(row), static_cast<int>(right - left + 1), 1, color, alpha);
    }
}
void Surface::blend(int x, int y, int width, int height, uint32_t color, int alpha) {
    if (width < 0 || height < 0) throw std::runtime_error("Graphics Error: rectangle size must not be negative");
    if (alpha < 0 || alpha > 255) throw std::runtime_error("Graphics Error: alpha must be 0..255");
    if (alpha == 0) return;
    if (alpha == 255) { rectangle(x, y, width, height, color); return; }
    Clip area = clip();
    int left = std::max(area.left, x), top = std::max(area.top, y);
    int right = static_cast<int>(std::min<int64_t>(area.right, int64_t(x) + width));
    int bottom = static_cast<int>(std::min<int64_t>(area.bottom, int64_t(y) + height));
    auto mix = [&](uint32_t under, int shift) {
        uint32_t a = (under >> shift) & 255, b = (color >> shift) & 255;
        return ((a * uint32_t(255 - alpha) + b * uint32_t(alpha) + 127) / 255) << shift;
    };
    for (int row = top; row < bottom; ++row)
        for (int col = left; col < right; ++col) {
            uint32_t& pixel = pixels_[size_t(row) * width_ + col];
            pixel = mix(pixel, 16) | mix(pixel, 8) | mix(pixel, 0);
        }
}
void Surface::line(int x1, int y1, int x2, int y2, uint32_t color, int alpha) {
    if (alpha < 0 || alpha > 255) throw std::runtime_error("Graphics Error: alpha must be 0..255");
    // Bresenham; points outside the surface are skipped, the line is not shortened.
    int64_t dx = std::llabs(int64_t(x2) - x1), dy = -std::llabs(int64_t(y2) - y1);
    int64_t sx = x1 < x2 ? 1 : -1, sy = y1 < y2 ? 1 : -1, error = dx + dy;
    int64_t x = x1, y = y1;
    Clip area = clip();
    if (dx > 20000 || -dy > 20000) throw std::runtime_error("Graphics Error: line is longer than 20000 pixels");
    for (;;) {
        if (x >= area.left && y >= area.top && x < area.right && y < area.bottom) blend(static_cast<int>(x), static_cast<int>(y), 1, 1, color, alpha);
        if (x == x2 && y == y2) break;
        int64_t twice = 2 * error;
        if (twice >= dy) { error += dy; x += sx; }
        if (twice <= dx) { error += dx; y += sy; }
    }
}
void Surface::frame(int x, int y, int width, int height, int thickness, uint32_t color) {
    if (width < 0 || height < 0) throw std::runtime_error("Graphics Error: frame size must not be negative");
    if (thickness < 1) throw std::runtime_error("Graphics Error: frame thickness must be at least 1");
    int t = std::min(thickness, std::max(1, std::min(width, height) / 2 + 1));
    rectangle(x, y, width, std::min(t, height), color);
    rectangle(x, static_cast<int>(std::max<int64_t>(y, int64_t(y) + height - t)), width, std::min(t, height), color);
    rectangle(x, y, std::min(t, width), height, color);
    rectangle(static_cast<int>(std::max<int64_t>(x, int64_t(x) + width - t)), y, std::min(t, width), height, color);
}
void Surface::ring(int x, int y, int radius, int thickness, uint32_t color, int alpha) {
    if (alpha < 0 || alpha > 255) throw std::runtime_error("Graphics Error: alpha must be 0..255");
    if (radius < 0 || radius > 8192) throw std::runtime_error("Graphics Error: radius must be 0..8192");
    if (thickness < 1) throw std::runtime_error("Graphics Error: ring thickness must be at least 1");
    int64_t outer = int64_t(radius) * radius, innerRadius = std::max<int64_t>(0, int64_t(radius) - thickness);
    int64_t inner = innerRadius * innerRadius;
    Clip area = clip();
    int64_t top = std::max<int64_t>(area.top, int64_t(y) - radius), bottom = std::min<int64_t>(area.bottom - 1, int64_t(y) + radius);
    int64_t left = std::max<int64_t>(area.left, int64_t(x) - radius), right = std::min<int64_t>(area.right - 1, int64_t(x) + radius);
    for (int64_t row = top; row <= bottom; ++row)
        for (int64_t col = left; col <= right; ++col) {
            int64_t d = (col - x) * (col - x) + (row - y) * (row - y);
            if (d <= outer && (d > inner || thickness > radius)) blend(static_cast<int>(col), static_cast<int>(row), 1, 1, color, alpha);
        }
}
void Surface::image(const Image& picture, int sx, int sy, int sw, int sh, int x, int y, int width, int height, int opacity) {
    if (sw <= 0 || sh <= 0 || width <= 0 || height <= 0 || opacity <= 0) return;
    if (sx < 0 || sy < 0 || int64_t(sx) + sw > picture.width || int64_t(sy) + sh > picture.height)
        throw std::runtime_error("Graphics Error: the part of the image is outside it");
    Clip area = clip();
    int left = static_cast<int>(std::max<int64_t>(area.left, x)), top = static_cast<int>(std::max<int64_t>(area.top, y));
    int right = static_cast<int>(std::min<int64_t>(area.right, int64_t(x) + width));
    int bottom = static_cast<int>(std::min<int64_t>(area.bottom, int64_t(y) + height));
    if (left >= right || top >= bottom) return;
    // Which source column each target column shows is the same on every row.
    bool unscaled = sw == width;
    thread_local std::vector<int> columns;
    if (!unscaled) {
        columns.resize(size_t(right - left));
        for (int col = left; col < right; ++col)
            columns[size_t(col - left)] = sx + static_cast<int>((int64_t(col - x) * sw) / width);
    }
    for (int row = top; row < bottom; ++row) {
        int from = sy + static_cast<int>((int64_t(row - y) * sh) / height);
        const uint32_t* source = picture.pixels.data() + size_t(from) * picture.width;
        uint32_t* target = pixels_.data() + size_t(row) * width_;
        for (int col = left; col < right; ++col) {
            uint32_t color = source[unscaled ? sx + (col - x) : columns[size_t(col - left)]];
            uint32_t alpha = opacity == 255 ? color >> 24 : ((color >> 24) * uint32_t(opacity) + 127) / 255;
            if (alpha == 0) continue;
            if (alpha == 255) {
                target[col] = color & 0xffffff;
                continue;
            }
            uint32_t under = target[col];
            auto mix = [&](int shift) {
                uint32_t a = (under >> shift) & 255, b = (color >> shift) & 255;
                return ((a * (255 - alpha) + b * alpha + 127) / 255) << shift;
            };
            target[col] = mix(16) | mix(8) | mix(0);
        }
    }
}

int Surface::textWidth(const std::string& value, int scale) {
    if (scale < 1 || scale > 32) throw std::runtime_error("Graphics Error: text scale must be 1..32");
    int64_t widest = 0, current = 0;
    for (size_t i = 0; i < value.size();) {
        if (nextCode(value, i) == U'\n') { current = 0; continue; }
        current += 6 * scale;
        widest = std::max(widest, current - scale); // no spacing after the last character
    }
    return static_cast<int>(std::min<int64_t>(widest, 1 << 30));
}
void Surface::text(int x, int y, const std::string& value, int scale, uint32_t color, int alpha) {
    if (alpha < 0 || alpha > 255) throw std::runtime_error("Graphics Error: alpha must be 0..255");
    if (scale < 1 || scale > 32) throw std::runtime_error("Graphics Error: text scale must be 1..32");
    if (value.size() > 65536) throw std::runtime_error("Graphics Error: text exceeds 65536 bytes");
    int64_t cursorX = x, cursorY = y;
    for (size_t i = 0; i < value.size();) {
        char32_t code = nextCode(value, i);
        if (code == U'\n') { cursorX = x; cursorY += 9 * scale; continue; }
        if (cursorX > -6 * scale && cursorX < width_ && cursorY > -7 * scale && cursorY < height_) {
            auto bitmap = glyph(code);
            for (int row = 0; row < 9; ++row)
                for (int col = 0; col < 5; ++col)
                    if (bitmap[row] & (1 << (4 - col))) blend(static_cast<int>(cursorX) + col * scale, static_cast<int>(cursorY) + row * scale, scale, scale, color, alpha);
        }
        cursorX += 6 * scale;
    }
}
} // namespace skrumi::graphics
